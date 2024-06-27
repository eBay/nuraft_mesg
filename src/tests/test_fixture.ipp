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
#pragma once

#include <memory>
#include <string>

#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <folly/init/Init.h>
#include <folly/executors/GlobalExecutor.h>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <nlohmann/json.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libnuraft/cluster_config.hxx"
#include "libnuraft/state_machine.hxx"
#include "nuraft_mesg/nuraft_mesg.hpp"
#include "nuraft_mesg/mesg_factory.hpp"

#include "test_state_manager.h"

SISL_OPTION_GROUP(data_service_test,  (num_iters, "", "num_iters", "iterations in long running test",
                                      ::cxxopts::value< uint32_t >()->default_value("100"), "number"));

SISL_LOGGING_INIT(NURAFTMESG_LOG_MODS)

SISL_OPTIONS_ENABLE(logging, data_service_test)

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
    bool data_svc_;

    std::map< group_id_t, std::shared_ptr< test_state_mgr > > state_mgr_map_;

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
        auto [it, happened] =
            state_mgr_map_.try_emplace(group_id, std::make_shared< test_state_mgr >(srv_id, id_, group_id));
        RELEASE_ASSERT(happened, "Failed!");
        if (data_svc_) it->second->register_data_service_apis(instance_.get());
        return std::static_pointer_cast< mesg_state_mgr >(it->second);
    }

    void map_peers(std::map< nuraft_mesg::peer_id_t, std::string > const& peers) {
        auto lg = std::scoped_lock(lookup_lock_);
        lookup_map_ = peers;
    }

    void start(bool data_svc_enabled = false) {
        data_svc_ = data_svc_enabled;
        auto params = Manager::Params();
        params.server_uuid_ = id_;
        params.mesg_port_ = port_;
        params.default_group_type_ = "test_type";
        params.max_message_size_ = 65 * 1024 * 1024;
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
    custom_factory(int const raft_threads, int const data_threads, nuraft_mesg::group_id_t const& name) :
            nuraft_mesg::group_factory::group_factory(raft_threads, data_threads, name, nullptr) {}

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

    void TearDown() override {
        app_3_->instance_->leave_group(group_id_);
        app_2_->instance_->leave_group(group_id_);
        app_1_->instance_->leave_group(group_id_);
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

        custom_factory_ = std::make_shared< custom_factory >(2, 2, group_id_);
        custom_factory_->map_peers(lookup_map);

        // Use custom factory to add Server 3
        auto factory = std::make_shared< mesg_factory >(custom_factory_, group_id_, "test_type");
        auto const dest_cfg = nuraft::srv_config(to_server_id(app_1_->id_), to_string(app_1_->id_));
        EXPECT_TRUE(factory->add_server(to_server_id(app_3_->id_), app_3_->id_, dest_cfg).get());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
};

int main(int argc, char* argv[]) {
    int parsed_argc = argc;
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, data_service_test);
    sisl::logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T.%e] [%n] [%^%l%$] [%t] %v");
    parsed_argc = 1;
    auto f = ::folly::Init(&parsed_argc, &argv, true);
    return RUN_ALL_TESTS();
    // sisl::GrpcAsyncClientWorker::shutdown_all();
}
