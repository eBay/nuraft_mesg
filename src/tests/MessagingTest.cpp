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
#include <boost/uuid/string_generator.hpp>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <nlohmann/json.hpp>

#include <sisl/utility/thread_buffer.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libnuraft/cluster_config.hxx"
#include "libnuraft/state_machine.hxx"
#include "mesg_factory.hpp"

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
    std::string id_;
    std::shared_ptr< Manager > instance_;

    TestApplication(std::string const& name, uint32_t port) : name_(name), port_(port) {
        id_ = to_string(boost::uuids::random_generator()());
    }
    ~TestApplication() override = default;

    std::string lookup_peer(std::string const& peer) override {
        auto lg = std::scoped_lock(lookup_lock_);
        return (lookup_map_.count(peer) > 0) ? lookup_map_[peer] : std::string();
    }

    std::shared_ptr< mesg_state_mgr > create_state_mgr(int32_t const srv_id, std::string const& group_id) override {
        auto [it, happened] = state_mgr_map.emplace(
            std::make_pair(group_id + "_" + name_, std::make_shared< test_state_mgr >(srv_id, id_, group_id)));
        return std::static_pointer_cast< mesg_state_mgr >(it->second);
    }

    void map_peers(std::map< std::string, std::string > const& peers) {
        auto lg = std::scoped_lock(lookup_lock_);
        lookup_map_ = peers;
    }

    void start() {
        auto params = Manager::Params();
        params.server_uuid_ = id_;
        params.mesg_port_ = port_;
        params.default_group_type_ = "test_type";
        instance_ = init_messaging(params, weak_from_this(), false);
        auto r_params = nuraft::raft_params()
                            .with_election_timeout_lower(elect_to_low)
                            .with_election_timeout_upper(elect_to_high)
                            .with_hb_interval(heartbeat_period)
                            .with_max_append_size(10)
                            .with_rpc_failure_backoff(rpc_backoff)
                            .with_auto_forwarding(true)
                            .with_snapshot_enabled(0);
        instance_->register_mgr_type("test_type", r_params);
    }

private:
    std::mutex lookup_lock_;
    std::map< std::string, std::string > lookup_map_;

    std::map< std::string, std::shared_ptr< test_state_mgr > > state_mgr_map;
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
    std::map< std::string, std::string > lookup_map;

    // Store state mgrs for each instance and group. key is "group_id" + "_sm{instance_number}".
    std::map< std::string, std::pair< std::shared_ptr< test_state_mgr >, Manager* > > state_mgr_map;

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

    void start() {
        app_1_->start();
        app_2_->start();
        app_3_->start();

        EXPECT_TRUE(!!app_1_->instance_->create_group("test_group", "test_type").get());

        EXPECT_TRUE(app_1_->instance_->add_member("test_group", app_2_->id_).get());
        EXPECT_TRUE(app_1_->instance_->add_member("test_group", app_3_->id_).get());
        std::this_thread::sleep_for(std::chrono::seconds(2));
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
    EXPECT_TRUE(app_1_->instance_->client_request("test_group", buf).get());

    app_3_->instance_->leave_group("test_group");
    app_2_->instance_->leave_group("test_group");
    app_1_->instance_->leave_group("test_group");
}

// Basic resiliency test (append_entries)
// TEST_F(MessagingFixture, ClientReset) {
//    // Simulate a Member crash
//    app_3_.reset();
//
//    // Commit message
//    auto buf = nuraft_mesg::create_message(nlohmann::json{
//        {"op_type", 2},
//    });
//    EXPECT_TRUE(app_1_->instance->client_request("test_group", buf).get());
//
//    uint32_t append_count{0};
//    auto register_params = consensus_component::register_params{
//        r_params,
//        [](int32_t const srv_id, std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
//            throw std::logic_error("Not Supposed To Happen");
//        },
//        [&append_count](std::function< void() > process_req) {
//            ++append_count;
//            process_req();
//        }};
//    instance_3->register_mgr_type("test_type", r_params);
//    auto params = consensus_component::params{id_3, ports[2], lookup_callback, "test_type"};
//    instance_3->start(params);
//    instance_3->join_group("test_group", "test_type", std::dynamic_pointer_cast< mesg_state_mgr >(sm_int_3));
//
//    auto const sm1_idx = sm_int_1->get_state_machine()->last_commit_index();
//    auto sm3_idx = sm_int_3->get_state_machine()->last_commit_index();
//    LOGINFO("SM1: {} / SM3: {}", sm1_idx, sm3_idx);
//    while (sm1_idx > sm3_idx) {
//        std::this_thread::sleep_for(std::chrono::seconds(1));
//        sm3_idx = sm_int_3->get_state_machine()->last_commit_index();
//        LOGINFO("SM1: {} / SM3: {}", sm1_idx, sm3_idx);
//    }
//    std::this_thread::sleep_for(std::chrono::seconds(5));
//    auto err = instance_3->client_request("test_group", buf);
//    if (err) { LOGERROR("Failed to commit: {}", err.message()); }
//    EXPECT_FALSE(err);
//
//    instance_3->leave_group("test_group");
//    instance_2->leave_group("test_group");
//    instance_1->leave_group("test_group");
//    EXPECT_GT(append_count, 0u);
//}

// Test sending a message for a group the messaging service is not aware of.
// TEST_F(MessagingFixture, UnknownGroup) {
//    EXPECT_FALSE(instance_1->add_member("unknown_group", to_string(boost::uuids::random_generator()()), true));
//
//    instance_1->leave_group("unknown_group");
//
//    auto buf = nuraft_mesg::create_message(nlohmann::json{
//        {"op_type", 2},
//    });
//    EXPECT_TRUE(instance_1->client_request("unknown_group", buf));
//}
//
// TEST_F(MessagingFixture, RemoveMember) {
//    EXPECT_TRUE(instance_1->rem_member("test_group", id_3));
//
//    auto buf = nuraft_mesg::create_message(nlohmann::json{
//        {"op_type", 2},
//    });
//    EXPECT_FALSE(instance_1->client_request("test_group", buf));
//}
//
// TEST_F(MessagingFixture, SyncAddMember) {
//    std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
//    instance_1->get_srv_config_all("test_group", srv_list);
//    EXPECT_EQ(srv_list.size(), 3u);
//    srv_list.clear();
//    instance_2->get_srv_config_all("test_group", srv_list);
//    EXPECT_EQ(srv_list.size(), 3u);
//    srv_list.clear();
//    instance_3->get_srv_config_all("test_group", srv_list);
//    EXPECT_EQ(srv_list.size(), 3u);
//
//    std::unique_ptr< Manager > instance_4 = std::make_unique< Manager >();
//    std::string id_4 = to_string(boost::uuids::random_generator()());
//    // generate random_port
//    get_random_ports(1u);
//    lookup_map.emplace(id_4, fmt::format("127.0.0.1:{}", ports[3]));
//    auto params = consensus_component::params{id_4, ports[3], lookup_callback, "test_type"};
//    std::shared_ptr< test_state_mgr > sm_int_4;
//    auto register_params = consensus_component::register_params{
//        r_params,
//        [this, srv_addr = id_4, &sm_int_4](int32_t const srv_id,
//                                           std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
//            sm_int_4 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
//            return std::static_pointer_cast< mesg_state_mgr >(sm_int_4);
//        }};
//    instance_4->start(params);
//    instance_4->register_mgr_type("test_type", r_params);
//    EXPECT_TRUE(instance_1->add_member("test_group", id_4, true /*wait for completion*/));
//
//    srv_list.clear();
//    instance_1->get_srv_config_all("test_group", srv_list);
//    EXPECT_EQ(srv_list.size(), 4u);
//}
//
// class DataServiceFixture : public MessagingFixtureBase {
// protected:
//    std::unique_ptr< Manager > instance_4;
//    std::unique_ptr< Manager > instance_5;
//    void SetUp() override {
//        MessagingFixtureBase::SetUp();
//        params.enable_data_service = true;
//        start();
//    }
//};
//
// static std::atomic< uint32_t > client_counter{0};
// void client_response_cb(sisl::io_blob const& incoming_buf) {
//    test_state_mgr::verify_data(incoming_buf);
//    client_counter++;
//}
//
// TEST_F(DataServiceFixture, DataServiceBasic) {
//    // create new servers
//    instance_4 = std::make_unique< Manager >();
//    std::string id_4 = to_string(boost::uuids::random_generator()());
//    instance_5 = std::make_unique< Manager >();
//    std::string id_5 = to_string(boost::uuids::random_generator()());
//    // generate random_port
//    get_random_ports(2u);
//    lookup_map.emplace(id_4, fmt::format("127.0.0.1:{}", ports[3]));
//    lookup_map.emplace(id_5, fmt::format("127.0.0.1:{}", ports[4]));
//    params.server_uuid = id_4, params.mesg_port = ports[3];
//    std::shared_ptr< test_state_mgr > sm_int_4;
//    auto register_params_4 = consensus_component::register_params{
//        r_params,
//        [this, srv_addr = id_4, &sm_int_4](int32_t const srv_id,
//                                           std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
//            auto [it, happened] = state_mgr_map.emplace(std::make_pair(
//                group_id + "_sm4",
//                std::make_pair(std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id), instance_4.get())));
//            sm_int_4 = it->second.first;
//            return std::static_pointer_cast< mesg_state_mgr >(sm_int_4);
//        }};
//    std::shared_ptr< test_state_mgr > sm_int_5;
//    auto register_params_5 = consensus_component::register_params{
//        r_params,
//        [this, srv_addr = id_5, &sm_int_5](int32_t const srv_id,
//                                           std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
//            auto [it, happened] = state_mgr_map.emplace(std::make_pair(
//                group_id + "_sm5",
//                std::make_pair(std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id), instance_5.get())));
//            sm_int_5 = it->second.first;
//            return std::static_pointer_cast< mesg_state_mgr >(sm_int_5);
//        }};
//    instance_4->start(params);
//    instance_4->register_mgr_type("test_type", r_params);
//    params.server_uuid = id_5, params.mesg_port = ports[4];
//    instance_5->start(params);
//    instance_5->register_mgr_type("test_type", r_params);
//
//    // create new group
//    instance_4->create_group("data_service_test_group", "test_type");
//    EXPECT_TRUE(instance_4->add_member("data_service_test_group", id_1, true));
//    EXPECT_TRUE(instance_4->add_member("data_service_test_group", id_2, true));
//    EXPECT_TRUE(instance_4->add_member("data_service_test_group", id_5, true));
//
//    for (auto& [key, smgr] : state_mgr_map) {
//        smgr.first->register_data_service_apis(smgr.second);
//    }
//
//    io_blob_list_t cli_buf;
//    test_state_mgr::fill_data_vec(cli_buf);
//
//    auto sm1 = state_mgr_map["test_group_sm1"].first;
//    auto sm4 = state_mgr_map["data_service_test_group_sm4"].first;
//
//    std::string const SEND_DATA{"send_data"};
//    std::string const REQUEST_DATA{"request_data"};
//
//    EXPECT_FALSE(sm1->data_service_request(SEND_DATA, cli_buf, client_response_cb));
//    EXPECT_FALSE(sm4->data_service_request(SEND_DATA, cli_buf, client_response_cb));
//    EXPECT_FALSE(sm1->data_service_request(REQUEST_DATA, cli_buf, client_response_cb));
//    std::this_thread::sleep_for(std::chrono::seconds(1));
//
//    // add a new member to data_service_test_group and check if repl_ctx4 sends data to newly added member
//    EXPECT_TRUE(instance_4->add_member("data_service_test_group", id_3, true));
//    auto sm3 = state_mgr_map["data_service_test_group_sm3"].first;
//    sm3->register_data_service_apis(instance_3.get());
//    EXPECT_FALSE(sm4->data_service_request(SEND_DATA, cli_buf, client_response_cb));
//    std::this_thread::sleep_for(std::chrono::seconds(1));
//
//    // the count is 4 (2 methods from group test_group) + 7 (from data_service_test_group)
//    EXPECT_EQ(test_state_mgr::get_server_counter(), 11);
//    EXPECT_EQ(client_counter, 11);
//
//    // free client buf
//    for (auto& buf : cli_buf) {
//        buf.buf_free();
//    }
//}

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
