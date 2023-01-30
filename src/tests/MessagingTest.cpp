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
#include <random>

#include <sisl/utility/thread_buffer.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libnuraft/cluster_config.hxx"
#include "libnuraft/state_machine.hxx"
#include "messaging.hpp"

#include "test_state_manager.h"
#include <sisl/fds/buffer.hpp>

SISL_LOGGING_INIT(nuraft, nuraft_mesg, grpc_server)

SISL_OPTIONS_ENABLE(logging)

constexpr auto rpc_backoff = 50;
constexpr auto heartbeat_period = 100;
constexpr auto elect_to_low = heartbeat_period * 2;
constexpr auto elect_to_high = elect_to_low * 2;

namespace nuraft_mesg {

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

class MessagingFixture : public ::testing::Test {
protected:
    std::unique_ptr< service > instance_1;
    std::unique_ptr< service > instance_2;
    std::unique_ptr< service > instance_3;

    std::shared_ptr< test_state_mgr > sm_int_1;
    std::shared_ptr< test_state_mgr > sm_int_2;
    std::shared_ptr< test_state_mgr > sm_int_3;

    std::string id_1;
    std::string id_2;
    std::string id_3;

    std::vector< uint32_t > ports;

    std::map< std::string, std::string > lookup_map;
    std::function< std::string(std::string const&) > lookup_callback;
    nuraft::raft_params r_params;

    uint32_t get_random_num() {
        static std::random_device dev;
        static std::mt19937 rng(dev());
        std::uniform_int_distribution< std::mt19937::result_type > dist(1001u, 99999u);
        return dist(rng);
    }

    void get_random_ports(const uint16_t n) {
        auto cur_size = ports.size();
        for (; ports.size() < cur_size + n;) {
            uint32_t r = get_random_num();
            if (std::find(ports.begin(), ports.end(), r) == ports.end()) { ports.emplace_back(r); }
        }
    }

    void SetUp() override {
        id_1 = to_string(boost::uuids::random_generator()());
        id_2 = to_string(boost::uuids::random_generator()());
        id_3 = to_string(boost::uuids::random_generator()());

        instance_1 = std::make_unique< service >();
        instance_2 = std::make_unique< service >();
        instance_3 = std::make_unique< service >();

        // generate 3 random port numbers
        get_random_ports(3u);

        lookup_map.emplace(id_1, fmt::format("127.0.0.1:{}", ports[0]));
        lookup_map.emplace(id_2, fmt::format("127.0.0.1:{}", ports[1]));
        lookup_map.emplace(id_3, fmt::format("127.0.0.1:{}", ports[2]));
        lookup_callback = [this](std::string const& id) -> std::string {
            return (lookup_map.count(id) > 0) ? lookup_map[id] : std::string();
        };

        auto params = consensus_component::params{id_1, ports[0], lookup_callback, "test_type"};
        instance_1->start(params);

        // RAFT server parameters
        r_params.with_election_timeout_lower(elect_to_low)
            .with_election_timeout_upper(elect_to_high)
            .with_hb_interval(heartbeat_period)
            .with_max_append_size(10)
            .with_rpc_failure_backoff(rpc_backoff)
            .with_auto_forwarding(true)
            .with_snapshot_enabled(0);

        auto register_params = consensus_component::register_params{
            r_params,
            [this, srv_addr = id_1](int32_t const srv_id,
                                    std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
                sm_int_1 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
                return std::static_pointer_cast< mesg_state_mgr >(sm_int_1);
            }};
        instance_1->register_mgr_type("test_type", register_params);

        params.server_uuid = id_2;
        params.mesg_port = ports[1];
        register_params.create_state_mgr =
            [this, srv_addr = id_2](int32_t const srv_id,
                                    std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            sm_int_2 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(sm_int_2);
        };
        instance_2->start(params);
        instance_2->register_mgr_type("test_type", register_params);

        params.server_uuid = id_3;
        params.mesg_port = ports[2];
        register_params.create_state_mgr =
            [this, srv_addr = id_3](int32_t const srv_id,
                                    std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            sm_int_3 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(sm_int_3);
        };
        instance_3->start(params);
        instance_3->register_mgr_type("test_type", register_params);

        instance_1->create_group("test_group", "test_type");

        EXPECT_TRUE(instance_1->add_member("test_group", id_2, true));
        EXPECT_TRUE(instance_1->add_member("test_group", id_3, true));
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    void TearDown() override {
        instance_1.reset();
        instance_2.reset();
        instance_3.reset();
    }

public:
};

// Basic client request (append_entries)
TEST_F(MessagingFixture, ClientRequest) {
    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_FALSE(instance_1->client_request("test_group", buf));

    instance_3->leave_group("test_group");
    instance_2->leave_group("test_group");
    instance_1->leave_group("test_group");
}

// Basic resiliency test (append_entries)
TEST_F(MessagingFixture, ClientReset) {
    // Simulate a Member crash
    instance_3 = std::make_unique< service >();

    // Commit message
    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_FALSE(instance_1->client_request("test_group", buf));

    uint32_t append_count{0};
    auto register_params = consensus_component::register_params{
        r_params,
        [](int32_t const srv_id, std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            throw std::logic_error("Not Supposed To Happen");
        },
        [&append_count](std::function< void() > process_req) {
            ++append_count;
            process_req();
        }};
    instance_3->register_mgr_type("test_type", register_params);
    auto params = consensus_component::params{id_3, ports[2], lookup_callback, "test_type"};
    instance_3->start(params);
    instance_3->join_group("test_group", "test_type", std::dynamic_pointer_cast< mesg_state_mgr >(sm_int_3));

    auto const sm1_idx = sm_int_1->get_state_machine()->last_commit_index();
    auto sm3_idx = sm_int_3->get_state_machine()->last_commit_index();
    LOGINFO("SM1: {} / SM3: {}", sm1_idx, sm3_idx);
    while (sm1_idx > sm3_idx) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        sm3_idx = sm_int_3->get_state_machine()->last_commit_index();
        LOGINFO("SM1: {} / SM3: {}", sm1_idx, sm3_idx);
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
    auto err = instance_3->client_request("test_group", buf);
    if (err) { LOGERROR("Failed to commit: {}", err.message()); }
    EXPECT_FALSE(err);

    instance_3->leave_group("test_group");
    instance_2->leave_group("test_group");
    instance_1->leave_group("test_group");
    EXPECT_GT(append_count, 0u);
}

// Test sending a message for a group the messaging service is not aware of.
TEST_F(MessagingFixture, UnknownGroup) {
    EXPECT_FALSE(instance_1->add_member("unknown_group", to_string(boost::uuids::random_generator()()), true));

    instance_1->leave_group("unknown_group");

    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_TRUE(instance_1->client_request("unknown_group", buf));
}

TEST_F(MessagingFixture, RemoveMember) {
    EXPECT_TRUE(instance_1->rem_member("test_group", id_3));

    auto buf = nuraft_mesg::create_message(nlohmann::json{
        {"op_type", 2},
    });
    EXPECT_FALSE(instance_1->client_request("test_group", buf));
}

TEST_F(MessagingFixture, SyncAddMember) {
    std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
    instance_1->get_srv_config_all("test_group", srv_list);
    EXPECT_EQ(srv_list.size(), 3u);
    srv_list.clear();
    instance_2->get_srv_config_all("test_group", srv_list);
    EXPECT_EQ(srv_list.size(), 3u);
    srv_list.clear();
    instance_3->get_srv_config_all("test_group", srv_list);
    EXPECT_EQ(srv_list.size(), 3u);

    std::unique_ptr< service > instance_4 = std::make_unique< service >();
    std::string id_4 = to_string(boost::uuids::random_generator()());
    // generate random_port
    get_random_ports(1u);
    lookup_map.emplace(id_4, fmt::format("127.0.0.1:{}", ports[3]));
    auto params = consensus_component::params{id_4, ports[3], lookup_callback, "test_type"};
    std::shared_ptr< test_state_mgr > sm_int_4;
    auto register_params = consensus_component::register_params{
        r_params,
        [this, srv_addr = id_4, &sm_int_4](int32_t const srv_id,
                                           std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            sm_int_4 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(sm_int_4);
        }};
    instance_4->start(params);
    instance_4->register_mgr_type("test_type", register_params);
    EXPECT_TRUE(instance_1->add_member("test_group", id_4, true /*wait for completion*/));

    srv_list.clear();
    instance_1->get_srv_config_all("test_group", srv_list);
    EXPECT_EQ(srv_list.size(), 4u);
}

static std::string const SEND_DATA{"send_data"};
static std::string const REQUEST_DATA{"request_data"};
static std::atomic< uint32_t > server_counter{0};
static std::atomic< uint32_t > client_counter{0};
static int const data_size{8};
static std::vector< uint32_t > data_vec;

bool receive_data(sisl::io_blob const& incoming_buf) {
    EXPECT_EQ(incoming_buf.size / sizeof(uint32_t), data_size);
    for (size_t read_sz{0}; read_sz < incoming_buf.size; read_sz += sizeof(uint32_t)) {
        uint32_t const data{*reinterpret_cast< uint32_t* >(incoming_buf.bytes + read_sz)};
        EXPECT_EQ(data, data_vec[read_sz / sizeof(uint32_t)]);
    }
    server_counter++;
    return true;
}
bool request_data(sisl::io_blob const& incoming_buf) {
    server_counter++;
    return true;
}
void client_response_cb(sisl::io_blob const& incoming_buf) { client_counter++; }

TEST_F(MessagingFixture, DataServiceBasic) {
    // create new servers
    std::unique_ptr< service > instance_4 = std::make_unique< service >();
    std::string id_4 = to_string(boost::uuids::random_generator()());
    std::unique_ptr< service > instance_5 = std::make_unique< service >();
    std::string id_5 = to_string(boost::uuids::random_generator()());
    // generate random_port
    get_random_ports(2u);
    lookup_map.emplace(id_4, fmt::format("127.0.0.1:{}", ports[3]));
    lookup_map.emplace(id_5, fmt::format("127.0.0.1:{}", ports[4]));
    auto params = consensus_component::params{id_4, ports[3], lookup_callback, "test_type"};
    std::shared_ptr< test_state_mgr > sm_int_4;
    auto register_params_4 = consensus_component::register_params{
        r_params,
        [this, srv_addr = id_4, &sm_int_4](int32_t const srv_id,
                                           std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            sm_int_4 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(sm_int_4);
        }};
    std::shared_ptr< test_state_mgr > sm_int_5;
    auto register_params_5 = consensus_component::register_params{
        r_params,
        [this, srv_addr = id_5, &sm_int_5](int32_t const srv_id,
                                           std::string const& group_id) -> std::shared_ptr< mesg_state_mgr > {
            sm_int_5 = std::make_shared< test_state_mgr >(srv_id, srv_addr, group_id);
            return std::static_pointer_cast< mesg_state_mgr >(sm_int_5);
        }};
    instance_4->start(params);
    instance_4->register_mgr_type("test_type", register_params_4);
    params.server_uuid = id_5, params.mesg_port = ports[4];
    instance_5->start(params);
    instance_5->register_mgr_type("test_type", register_params_5);

    // create new group
    instance_4->create_group("data_service_test_group", "test_type");
    EXPECT_TRUE(instance_4->add_member("data_service_test_group", id_1, true));
    EXPECT_TRUE(instance_4->add_member("data_service_test_group", id_2, true));
    EXPECT_TRUE(instance_4->add_member("data_service_test_group", id_5, true));

    // bind data channel request methods
    instance_1->bind_data_service_request(SEND_DATA, receive_data);
    instance_1->bind_data_service_request(REQUEST_DATA, request_data);
    instance_2->bind_data_service_request(SEND_DATA, receive_data);
    instance_2->bind_data_service_request(REQUEST_DATA, request_data);
    instance_3->bind_data_service_request(SEND_DATA, receive_data);
    instance_3->bind_data_service_request(REQUEST_DATA, request_data);
    instance_4->bind_data_service_request(SEND_DATA, receive_data);
    instance_5->bind_data_service_request(SEND_DATA, receive_data);

    io_blob_list_t cli_buf;
    for (int i = 0; i < data_size; i++) {
        cli_buf.emplace_back(sizeof(uint32_t));
        uint32_t* const write_buf{reinterpret_cast< uint32_t* >(cli_buf[i].bytes)};
        data_vec.emplace_back(get_random_num());
        *write_buf = data_vec.back();
    }

    instance_1->data_service_request("test_group", SEND_DATA, client_response_cb, cli_buf);
    // instance_4->data_service_request("data_service_test_group", SEND_DATA, client_response_cb, cli_buf);
    // instance_1->data_service_request("test_group", REQUEST_DATA, client_response_cb, cli_buf);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // the count is 4 (2 methods from group test_group) + 3 (from data_service_test_group)
    // EXPECT_EQ(server_counter, 7);
    // EXPECT_EQ(client_counter, 7);
}

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
