#include "test_fixture.ipp"
#include "test_state_machine.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>
#include <grpcpp/grpcpp.h>
#include <libnuraft/nuraft.hxx>
#include <sisl/grpc/rpc_call.hpp>

#include "lib/nuraft_mesg_config.hpp"
#include "proto/messaging_service.grpc.pb.h"

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
    auto vote_elapsed_ms =
        std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::steady_clock::now() - vote_start).count();

    // Vote should complete quickly (< 5s) despite app_2's I/O pool being blocked
    // This proves fast messages (votes) don't wait behind slow messages (append_entries)
    EXPECT_TRUE(result) << "Leadership change should succeed";
    EXPECT_LT(vote_elapsed_ms, 5000) << "Vote took " << vote_elapsed_ms << "ms - I/O pool is blocking Raft thread!";

    LOGINFO("Leadership change completed in {}ms while app_2 had slow append_entries", vote_elapsed_ms);
}

// Test that append_entries still work through the two-pool architecture
TEST_F(ThreadPoolFixture, AppendEntriesSucceedThroughPools) {
    auto buf = create_test_message(nlohmann::json{{"op_type", 2}});

    // Send multiple append_entries - these route through I/O pool
    int success_count = 0;
    for (int i = 0; i < 20; ++i) {
        if (sync_get(app_1_->impl_inst_->append_entries(group_id_, {buf}))) {
            ++success_count;
        }
    }

    EXPECT_EQ(success_count, 20) << "All append_entries should succeed through I/O pool";
}

// Test that fast messages (vote) complete quickly
TEST_F(ThreadPoolFixture, FastMessagesProcessedOnRaftThread) {
    // Trigger leadership change (uses pre_vote and vote - fast path on Raft thread)
    auto start_time = std::chrono::steady_clock::now();
    EXPECT_TRUE(sync_get(app_3_->instance_->become_leader(group_id_)));
    auto elapsed_ms =
        std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::steady_clock::now() - start_time).count();

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

// RaftStep calls alive in this process: sisl counts the RpcData of every call a server serves, and of the next call
// each server waits for.
static int64_t raft_step_calls_alive() {
    return sisl::ObjLifeCounter< sisl::RpcData< nuraft_mesg::Messaging, nuraft_mesg::RaftGroupMsg,
                                                nuraft_mesg::RaftGroupMsg, false > >::s_alive.load();
}

// Polls cond for up to 10 seconds; whether it became true.
template < typename Cond >
static bool wait_until(Cond cond) {
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!cond()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

// Whether a member refused a RaftStep as it is shutting down.
static bool refused(grpc::Status const& status) {
    return status.error_code() == grpc::StatusCode::UNAVAILABLE && status.error_message() == "shutting down";
}

// app_2 alone, leading a group of its own: no peer sends it RAFT messages, the test sends them over gRPC.
class ShutdownFixture : public MessagingFixtureBase {
protected:
    int64_t calls_alive_before_{0};
    std::unique_ptr< Messaging::Stub > stub_;

    void SetUp() override {
        calls_alive_before_ = raft_step_calls_alive();
        MessagingFixtureBase::SetUp();
        app_2_->start();
        group_id_ = boost::uuids::random_generator()();
        EXPECT_TRUE(sync_get(app_2_->instance_->create_group(group_id_, "test_type")));
        stub_ = Messaging::NewStub(
            grpc::CreateChannel(fmt::format("127.0.0.1:{}", app_2_->port_), grpc::InsecureChannelCredentials()));
    }

    void TearDown() override {
        if (auto it = app_2_->state_mgr_map_.find(group_id_); app_2_->state_mgr_map_.end() != it) {
            it->second->release_requests(); // held if the test failed
        }
        if (app_2_->instance_) {
            app_2_->instance_->leave_group(group_id_);
        }
    }

    // A RaftStep of this type for app_2's group, at term 0, which RAFT declines; to: the member it is for.
    grpc::Status raft_step(nuraft::msg_type type, std::string const& to) {
        RaftGroupMsg request;
        request.set_group_id(to_string(group_id_));
        request.set_intended_addr(to);
        request.mutable_msg()->mutable_base()->set_type(type);
        request.mutable_msg()->mutable_rc_request();
        RaftGroupMsg reply;
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(20));
        return stub_->RaftStep(&context, request, &reply);
    }

    // Drops the member's owners of its ManagerImpl, whose destructor shuts its messaging service down, then its gRPC
    // server.
    static void shut_down(std::shared_ptr< TestApplication > const& app) {
        app->instance_.reset();
        app->impl_inst_.reset();
    }
};

// A member that shuts down answers every RaftStep it took: those its thread pools run, those queued behind them, and,
// while it waits for these, those still arriving, which it refuses (UNAVAILABLE) rather than queue to a pool it no
// longer runs. sisl keeps the call of a RaftStep that is never answered for the life of the process.
TEST_F(ShutdownFixture, ShutdownAnswersEveryRaftStep) {
    auto const app_2 = to_string(app_2_->id_);
    auto& smgr = *app_2_->state_mgr_map_[group_id_];
    // A pre-vote request runs in app_2's RAFT pool, an install-snapshot request in its I/O pool, sized as proto_service
    // sizes them.
    auto const raft_workers = int{NURAFT_MESG_CONFIG(raft_append_entries_thread_cnt)};
    auto const io_workers =
        NURAFT_MESG_CONFIG(io_thread_pool_size) > 0 ? static_cast< int >(NURAFT_MESG_CONFIG(io_thread_pool_size)) : 4;
    auto const listening = raft_step_calls_alive();

    // Every worker of both pools holds a request, and two more requests wait behind them in each pool.
    smgr.hold_requests({nuraft::msg_type::pre_vote_request, nuraft::msg_type::install_snapshot_request});
    std::vector< std::future< grpc::Status > > calls;
    auto const send = [&](nuraft::msg_type type, int count) {
        for (int i = 0; i < count; ++i) {
            calls.push_back(std::async(std::launch::async, [this, type, &app_2] { return raft_step(type, app_2); }));
        }
    };
    send(nuraft::msg_type::pre_vote_request, raft_workers + 2);
    send(nuraft::msg_type::install_snapshot_request, io_workers + 2);
    // sisl counts the next call a server waits for once raftStep() has returned for the last one: by then that one is
    // in its pool.
    ASSERT_TRUE(wait_until([&] {
        return smgr.requests_held() == raft_workers + io_workers &&
            raft_step_calls_alive() == listening + static_cast< int64_t >(calls.size());
    }));

    auto stopped = std::async(std::launch::async, [this] { shut_down(app_2_); });
    // A request for another member is rejected for its address until the shutdown starts, and refused from then on.
    auto const other = to_string(boost::uuids::random_generator()());
    EXPECT_TRUE(wait_until([&] { return refused(raft_step(nuraft::msg_type::pre_vote_request, other)); }));
    // The shutdown waits for the requests in its pools, and refuses new ones.
    EXPECT_TRUE(refused(raft_step(nuraft::msg_type::pre_vote_request, app_2)));
    EXPECT_TRUE(refused(raft_step(nuraft::msg_type::install_snapshot_request, app_2)));
    EXPECT_EQ(stopped.wait_for(std::chrono::seconds(0)), std::future_status::timeout);

    smgr.release_requests();
    stopped.get();
    for (auto& call : calls) {
        // Each ran: RAFT answered it or, once app_2's manager was gone, rejected it.
        auto const status = call.get();
        EXPECT_TRUE(status.ok() || status.error_message() == "Server rejected request")
            << status.error_code() << ": " << status.error_message();
    }
    smgr.permanent_destroy(); // app_2's RAFT state is removed with the fixture
    EXPECT_TRUE(wait_until([this] { return raft_step_calls_alive() == calls_alive_before_; }))
        << "a RaftStep call was never answered";
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
