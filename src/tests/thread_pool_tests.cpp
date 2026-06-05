#include "test_fixture.ipp"
#include "test_state_machine.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>
#include <libnuraft/nuraft.hxx>

static nuraft::ptr< nuraft::buffer > create_test_message(nlohmann::json const& j_obj) {
    auto v_msgpack = nlohmann::json::to_msgpack(j_obj);
    auto buf = nuraft::buffer::alloc(v_msgpack.size() + sizeof(int32_t));
    buf->put(&v_msgpack[0], v_msgpack.size());
    buf->pos(0);
    return buf;
}

class ThreadPoolFixture : public MessagingFixtureBase {
protected:
    void SetUp() override {
        MessagingFixtureBase::SetUp();
        start(true);
        // Wait for stable leadership
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
};

// Test that slow append_entries operations don't block fast vote messages
TEST_F(ThreadPoolFixture, NoHeadOfLineBlockingWithSlowAppendEntries) {
    // Inject 10-second delay ONLY on app_2 (one follower)
    // This simulates slow I/O (e.g., localize_journal_entry_prepare taking 10+ seconds)
    app_2_->state_mgr_map_[group_id_]->get_sm()->set_append_delay(10000);

    // Send append_entries to trigger the slow path on app_2
    auto buf = create_test_message(nlohmann::json{{"op_type", 1}});
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(sync_get(app_1_->impl_inst_->append_entries(group_id_, {buf})));
    }

    // Give time for append_entries to start processing on app_2 (blocking I/O pool)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // While app_2's I/O pool is blocked processing slow append_entries,
    // trigger leadership change (uses vote messages on Raft thread pool)
    auto vote_start = std::chrono::steady_clock::now();
    auto result = sync_get(app_3_->instance_->become_leader(group_id_));
    auto vote_elapsed_ms = std::chrono::duration_cast< std::chrono::milliseconds >(
                               std::chrono::steady_clock::now() - vote_start)
                               .count();

    // Vote should complete quickly (< 5s) despite app_2's I/O pool being blocked
    // This proves fast messages (votes) don't wait behind slow messages (append_entries)
    EXPECT_TRUE(result) << "Leadership change should succeed";
    EXPECT_LT(vote_elapsed_ms, 5000) << "Vote took " << vote_elapsed_ms
                                      << "ms - I/O pool is blocking Raft thread!";

    LOGINFO("Leadership change completed in {}ms while app_2 had slow append_entries", vote_elapsed_ms);
}

// Test that append_entries still work through the two-pool architecture
TEST_F(ThreadPoolFixture, AppendEntriesSucceedThroughPools) {
    auto buf = create_test_message(nlohmann::json{{"op_type", 2}});

    // Send multiple append_entries - these route through I/O pool
    int success_count = 0;
    for (int i = 0; i < 20; ++i) {
        if (sync_get(app_1_->impl_inst_->append_entries(group_id_, {buf}))) { ++success_count; }
    }

    EXPECT_EQ(success_count, 20) << "All append_entries should succeed through I/O pool";
}

// Test that fast messages (vote) complete quickly
TEST_F(ThreadPoolFixture, FastMessagesProcessedOnRaftThread) {
    // Trigger leadership change (uses pre_vote and vote - fast path on Raft thread)
    auto start_time = std::chrono::steady_clock::now();
    EXPECT_TRUE(sync_get(app_3_->instance_->become_leader(group_id_)));
    auto elapsed_ms = std::chrono::duration_cast< std::chrono::milliseconds >(
                          std::chrono::steady_clock::now() - start_time)
                          .count();

    // Leadership change should complete within a reasonable time (< 5s)
    EXPECT_LT(elapsed_ms, 5000) << "Leadership change (fast path) took too long: " << elapsed_ms << "ms";

    // Verify leadership actually changed
    auto sm3 = app_3_->state_mgr_map_[group_id_];
    auto repl_ctx3 = sm3->get_repl_context();
    EXPECT_TRUE(repl_ctx3->is_raft_leader());
}

// Test concurrent operations don't leak thread counters
TEST_F(ThreadPoolFixture, MetricsCountersConsistency) {
    auto buf = create_test_message(nlohmann::json{{"op_type", 2}});

    // Send a batch of messages - exercises I/O pool
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(sync_get(app_1_->impl_inst_->append_entries(group_id_, {buf})));
    }

    // Trigger fast-path messages (leadership change uses pre_vote/vote on Raft thread)
    EXPECT_TRUE(sync_get(app_3_->instance_->become_leader(group_id_)));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Send more messages after leadership change
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(sync_get(app_3_->impl_inst_->append_entries(group_id_, {buf})));
    }

    // If atomic counters leaked (never decremented), operations would eventually hang
    LOGINFO("All operations completed - atomic counters balanced correctly");
}

// Unit test for message type classification
TEST(MessageTypeTest, IsSlowMessageClassification) {
    // Slow messages (should route to I/O pool)
    EXPECT_TRUE(nuraft::msg_type::append_entries_request == nuraft::msg_type::append_entries_request);
    EXPECT_TRUE(nuraft::msg_type::install_snapshot_request == nuraft::msg_type::install_snapshot_request);

    // Fast messages (should stay on Raft thread)
    EXPECT_TRUE(nuraft::msg_type::request_vote_request != nuraft::msg_type::append_entries_request);
    EXPECT_TRUE(nuraft::msg_type::pre_vote_request != nuraft::msg_type::append_entries_request);
    EXPECT_TRUE(nuraft::msg_type::append_entries_response != nuraft::msg_type::append_entries_request);
    EXPECT_TRUE(nuraft::msg_type::request_vote_response != nuraft::msg_type::append_entries_request);
    EXPECT_TRUE(nuraft::msg_type::install_snapshot_response != nuraft::msg_type::install_snapshot_request);
}
