/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <memory>
#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <nlohmann/json.hpp>

#include <sisl/utility/thread_buffer.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libnuraft/cluster_config.hxx"
#include "libnuraft/state_machine.hxx"
#include "nuraft_mesg/common.hpp"
#include "nuraft_mesg/mesg_factory.hpp"

#include "test_state_manager.h"
#include <sisl/fds/buffer.hpp>

SISL_LOGGING_INIT(nuraft, nuraft_mesg, grpc_server)

SISL_OPTIONS_ENABLE(logging)

constexpr auto rpc_backoff = 50;
constexpr auto heartbeat_period = 100;
constexpr auto elect_to_low = heartbeat_period * 2;
constexpr auto elect_to_high = elect_to_low * 2;

namespace nuraft_mesg {

class TestApplication : public MessagingApplication, public std::enable_shared_from_this< TestApplication > {
public:
    std::string name_;
    uint32_t port_;
    boost::uuids::uuid id_;
    std::shared_ptr< Manager > instance_;

    TestApplication(std::string const& name, uint32_t port) : name_(name), port_(port) {
        id_ = boost::uuids::random_generator()();
    }
    ~TestApplication() override = default;

    void set_id(boost::uuids::uuid const& id) { id_ = id; }

    std::string lookup_peer(peer_id_t const& peer) override {
        auto lg = std::scoped_lock(lookup_lock_);
        return (lookup_map_.count(peer) > 0) ? lookup_map_[peer] : std::string();
    }

    std::shared_ptr< mesg_state_mgr > create_state_mgr(int32_t const srv_id, group_id_t const& group_id) override {
        return std::static_pointer_cast< mesg_state_mgr >(std::make_shared< test_state_mgr >(srv_id, id_, group_id));
    }

    void map_peers(std::map< nuraft_mesg::peer_id_t, std::string > const& peers) {
        auto lg = std::scoped_lock(lookup_lock_);
        lookup_map_ = peers;
    }

    void start(bool data_svc_enabled = false) {
        auto params = Manager::Params();
        params.server_uuid_ = id_;
        params.mesg_port_ = port_;
        params.default_group_type_ = "test_type";
        instance_ = init_messaging(params, weak_from_this(), data_svc_enabled);
        auto r_params = nuraft::raft_params()
                            .with_election_timeout_lower(elect_to_low)
                            .with_election_timeout_upper(elect_to_high)
                            .with_hb_interval(heartbeat_period)
                            .with_max_append_size(10)
                            .with_rpc_failure_backoff(rpc_backoff)
                            .with_auto_forwarding(false)
                            .with_snapshot_enabled(0);
        r_params.return_method_ = nuraft::raft_params::async_handler;
        instance_->register_mgr_type("test_type", r_params);
    }

private:
    std::mutex lookup_lock_;
    std::map< nuraft_mesg::peer_id_t, std::string > lookup_map_;
};

struct custom_factory : public nuraft_mesg::group_factory {
    custom_factory(int const threads, nuraft_mesg::group_id_t const& name) :
            nuraft_mesg::group_factory::group_factory(threads, name, nullptr) {}

    std::string lookupEndpoint(nuraft_mesg::peer_id_t const& peer) override {
        auto lg = std::scoped_lock(lookup_lock_);
        return (lookup_map_.count(peer) > 0) ? lookup_map_[peer] : std::string();
    }

    void map_peers(std::map< nuraft_mesg::peer_id_t, std::string > const& peers) {
        auto lg = std::scoped_lock(lookup_lock_);
        lookup_map_ = peers;
    }
    std::mutex lookup_lock_;
    std::map< nuraft_mesg::peer_id_t, std::string > lookup_map_;
};

extern nuraft::ptr< nuraft::cluster_config > fromClusterConfig(nlohmann::json const& cluster_config);

static nuraft::ptr< nuraft::buffer > create_message(nlohmann::json const& j_obj) {
    auto j_str = j_obj.dump();
    auto v_msgpack = nlohmann::json::to_msgpack(j_obj);
    auto buf = nuraft::buffer::alloc(v_msgpack.size() + sizeof(int32_t));
    buf->put(&v_msgpack[0], v_msgpack.size());
    buf->pos(0);
    return buf;
}

} // namespace nuraft_mesg

using namespace nuraft_mesg;
using testing::_;
using testing::Return;

class MessagingFixtureBase : public ::testing::Test {
protected:
    std::shared_ptr< TestApplication > app_1_;
    std::shared_ptr< TestApplication > app_2_;
    std::shared_ptr< TestApplication > app_3_;

    std::vector< uint32_t > ports;
    std::map< nuraft_mesg::peer_id_t, std::string > lookup_map;

    group_id_t group_id_;

    std::shared_ptr< custom_factory > custom_factory_;

    void get_random_ports(const uint16_t n) {
        auto cur_size = ports.size();
        for (; ports.size() < cur_size + n;) {
            uint32_t r = test_state_mgr::get_random_num();
            if (std::find(ports.begin(), ports.end(), r) == ports.end()) { ports.emplace_back(r); }
        }
    }

    void SetUp() override {
        // generate 3 random port numbers
        get_random_ports(3u);

        app_1_ = std::make_shared< TestApplication >("sm1", ports[0]);
        app_2_ = std::make_shared< TestApplication >("sm2", ports[1]);
        app_3_ = std::make_shared< TestApplication >("sm3", ports[2]);

        lookup_map.emplace(app_1_->id_, fmt::format("127.0.0.1:{}", ports[0]));
        lookup_map.emplace(app_2_->id_, fmt::format("127.0.0.1:{}", ports[1]));
        lookup_map.emplace(app_3_->id_, fmt::format("127.0.0.1:{}", ports[2]));

        app_1_->map_peers(lookup_map);
        app_2_->map_peers(lookup_map);
        app_3_->map_peers(lookup_map);
    }

    void start(bool data_svc_enabled = false) {
        app_1_->start(data_svc_enabled);
        app_2_->start(data_svc_enabled);
        app_3_->start(data_svc_enabled);

        group_id_ = boost::uuids::random_generator()();

        EXPECT_TRUE(app_1_->instance_->create_group(group_id_, "test_type").get());
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Use app1 to add Server 3
        auto add2 = app_1_->instance_->add_member(group_id_, app_2_->id_);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        EXPECT_TRUE(std::move(add2).get());

        custom_factory_ = std::make_shared< custom_factory >(2, group_id_);
        custom_factory_->map_peers(lookup_map);

        // Use custom factory to add Server 3
        auto factory = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
        auto const dest_cfg = nuraft::srv_config(to_server_id(app_1_->id_), to_string(app_1_->id_));
        EXPECT_TRUE(factory->add_server(to_server_id(app_3_->id_), app_3_->id_, dest_cfg).get());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
};

class MessagingFixture : public MessagingFixtureBase {
protected:
    void SetUp() override {
        MessagingFixtureBase::SetUp();
        start();
    }
};

// Basic client request (append_entries)
TEST_F(MessagingFixture, ClientRequest) {
    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_TRUE(app_1_->instance_->append_entries(group_id_, {buf}).get());

    app_3_->instance_->leave_group(group_id_);
    app_2_->instance_->leave_group(group_id_);
    app_1_->instance_->leave_group(group_id_);
}

// Basic resiliency test (append_entries)
TEST_F(MessagingFixture, MemberCrash) {
    // Simulate a Member crash
    auto our_id = app_3_->id_;
    app_3_.reset();

    // Commit message
    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    auto factory = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
    auto const dest_cfg_1 = nuraft::srv_config(to_server_id(app_1_->id_), to_string(app_1_->id_));
    auto const dest_cfg_2 = nuraft::srv_config(to_server_id(app_2_->id_), to_string(app_2_->id_));
    EXPECT_TRUE(factory->append_entry(buf, dest_cfg_1).get());
    EXPECT_TRUE(factory->append_entry(buf, dest_cfg_2).get());

    app_3_ = std::make_shared< TestApplication >("sm3", ports[2]);
    app_3_->set_id(our_id);
    app_3_->map_peers(lookup_map);
    app_3_->start();
    app_3_->instance_->join_group(group_id_, "test_type",
                                  std::static_pointer_cast< mesg_state_mgr >(std::make_shared< test_state_mgr >(
                                      nuraft_mesg::to_server_id(our_id), our_id, group_id_)));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_FALSE(app_3_->instance_->become_leader(boost::uuids::random_generator()()).get());
    EXPECT_TRUE(app_3_->instance_->become_leader(group_id_).get());
    EXPECT_TRUE(app_3_->instance_->append_entries(group_id_, {buf}).get());

    app_3_->instance_->leave_group(group_id_);
    app_2_->instance_->leave_group(group_id_);
    app_1_->instance_->leave_group(group_id_);
}

// Test sending a message for a group the messaging service is not aware of.
TEST_F(MessagingFixture, UnknownGroup) {
    auto add = app_1_->instance_->add_member(boost::uuids::random_generator()(), boost::uuids::random_generator()());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_FALSE(std::move(add).get());

    app_1_->instance_->leave_group(boost::uuids::random_generator()());

    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_FALSE(app_1_->instance_->append_entries(boost::uuids::random_generator()(), {buf}).get());
}

TEST_F(MessagingFixture, RemoveMember) {
    // Expect failure trying to remove unknown member
    auto factory = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
    auto const dest_cfg = nuraft::srv_config(to_server_id(app_1_->id_), to_string(app_1_->id_));
    EXPECT_FALSE(factory->rem_server(1000, dest_cfg).get());

    // Expect failure trying to remove unknown group
    EXPECT_FALSE(app_2_->instance_->rem_member(boost::uuids::random_generator()(), app_3_->id_).get());

    EXPECT_TRUE(app_1_->instance_->rem_member(group_id_, app_3_->id_).get());
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_TRUE(app_1_->instance_->append_entries(group_id_, {buf}).get());
}

TEST_F(MessagingFixture, SyncAddMember) {
    std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
    app_1_->instance_->get_srv_config_all(group_id_, srv_list);
    EXPECT_EQ(srv_list.size(), 3u);
    srv_list.clear();
    app_2_->instance_->get_srv_config_all(group_id_, srv_list);
    EXPECT_EQ(srv_list.size(), 3u);
    srv_list.clear();
    app_3_->instance_->get_srv_config_all(group_id_, srv_list);
    EXPECT_EQ(srv_list.size(), 3u);

    get_random_ports(1u);
    auto app_4 = std::make_shared< TestApplication >("sm4", ports[3]);
    lookup_map.emplace(app_4->id_, fmt::format("127.0.0.1:{}", ports[3]));
    app_1_->map_peers(lookup_map);
    app_2_->map_peers(lookup_map);
    app_3_->map_peers(lookup_map);
    app_4->map_peers(lookup_map);
    app_4->start();
    auto add = app_1_->instance_->add_member(group_id_, app_4->id_);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(std::move(add).get());

    srv_list.clear();
    app_1_->instance_->get_srv_config_all(group_id_, srv_list);
    EXPECT_EQ(srv_list.size(), 4u);
}

// class DataServiceFixture : public MessagingFixtureBase {
// protected:
//     void SetUp() override {
//         MessagingFixtureBase::SetUp();
//         start(true);
//     }
// };
//
// TEST_F(DataServiceFixture, DataServiceBasic) {
//     get_random_ports(2u);
//     // create new servers
//     auto app_4 = std::make_shared< TestApplication >("sm4", ports[3]);
//     lookup_map.emplace(app_4->id_, fmt::format("127.0.0.1:{}", ports[3]));
//     app_1_->map_peers(lookup_map);
//     app_2_->map_peers(lookup_map);
//     app_3_->map_peers(lookup_map);
//     app_4->map_peers(lookup_map);
//     app_4->start(true);
//     auto add4 = app_1_->instance_->add_member("test_group", app_4->id_);
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     EXPECT_TRUE(std::move(add4).get());
//
//     auto app_5 = std::make_shared< TestApplication >("sm5", ports[4]);
//     lookup_map.emplace(app_5->id_, fmt::format("127.0.0.1:{}", ports[4]));
//     app_1_->map_peers(lookup_map);
//     app_2_->map_peers(lookup_map);
//     app_3_->map_peers(lookup_map);
//     app_4->map_peers(lookup_map);
//     app_5->map_peers(lookup_map);
//     app_5->start(true);
//     auto add5 = app_1_->instance_->add_member("test_group", app_5->id_);
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     EXPECT_TRUE(std::move(add5).get());
//
//     // create new group
//     app_4->instance_->create_group("data_service_test_group", "test_type");
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     auto add1 = app_4->instance_->add_member("data_service_test_group", app_1_->id_);
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     EXPECT_TRUE(std::move(add1).get());
//     auto add2 = app_4->instance_->add_member("data_service_test_group", app_2_->id_);
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     EXPECT_TRUE(std::move(add2).get());
//     add5 = app_4->instance_->add_member("data_service_test_group", app_5->id_);
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     EXPECT_TRUE(std::move(add5).get());
//
//     for (auto& [key, smgr] : state_mgr_map) {
//         smgr.first->register_data_service_apis(smgr.second);
//     }
//
//     io_blob_list_t cli_buf;
//     test_state_mgr::fill_data_vec(cli_buf);
//
//     auto sm1 = state_mgr_map["test_group_sm1"].first;
//     auto sm4 = state_mgr_map["data_service_test_group_sm4"].first;
//
//     std::string const SEND_DATA{"send_data"};
//     std::string const REQUEST_DATA{"request_data"};
//
//     std::vector< NullAsyncResult > results;
//     results.push_back(sm1->data_service_request(SEND_DATA, cli_buf).deferValue([](auto e) -> NullResult {
//         test_state_mgr::verify_data(e.value());
//         return folly::Unit();
//     }));
//     results.push_back(sm4->data_service_request(SEND_DATA, cli_buf).deferValue([](auto e) -> NullResult {
//         test_state_mgr::verify_data(e.value());
//         return folly::Unit();
//     }));
//
//     results.push_back(sm1->data_service_request(REQUEST_DATA, cli_buf).deferValue([](auto e) -> NullResult {
//         test_state_mgr::verify_data(e.value());
//         return folly::Unit();
//     }));
//     folly::collectAll(results).via(folly::getGlobalCPUExecutor()).get();
//
//     // add a new member to data_service_test_group and check if repl_ctx4 sends data to newly added member
//     auto add_3 = app_4->instance_->add_member("data_service_test_group", app_3_->id_);
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     EXPECT_TRUE(std::move(add_3).get());
//     auto sm3 = state_mgr_map["data_service_test_group_sm3"].first;
//     sm3->register_data_service_apis(app_3_->instance_.get());
//     sm4->data_service_request(SEND_DATA, cli_buf)
//         .deferValue([](auto e) -> folly::Unit {
//             test_state_mgr::verify_data(e.value());
//             return folly::Unit();
//         })
//         .get();
//
//     // the count is 4 (2 methods from group test_group) + 7 (from data_service_test_group)
//     EXPECT_EQ(test_state_mgr::get_server_counter(), 11);
//
//     // free client buf
//     for (auto& buf : cli_buf) {
//         buf.buf_free();
//     }
// }

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T.%f%z] [%^%l%$] [%t] %v");
    sisl::logging::GetLogger()->flush_on(spdlog::level::level_enum::err);

    auto ret = RUN_ALL_TESTS();
    sisl::GrpcAsyncClientWorker::shutdown_all();

    return ret;
}
