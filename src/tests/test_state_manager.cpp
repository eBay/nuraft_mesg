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
#include "test_state_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <system_error>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <libnuraft/state_machine.hxx>
#include <sisl/grpc/generic_service.hpp>
#include <sisl/grpc/rpc_client.hpp>

#include "nuraft_mesg/nuraft_mesg.hpp"
#include "jungle_logstore/jungle_log_store.h"

#include "test_state_machine.h"

#define STATE_PATH(g, s, f) fmt::format(FMT_STRING("{}_s{}{}"), (g), (s), (f))

using json = nlohmann::json;

std::vector< uint32_t > test_state_mgr::data_vec;

std::error_condition jsonObjectFromFile(std::string const& filename, json& json_object) {
    std::ifstream istrm(filename, std::ios::binary);
    if (!istrm.is_open()) {
        return std::make_error_condition(std::errc::no_such_file_or_directory);
    } else if (!istrm.is_open()) {
        return std::make_error_condition(std::errc::io_error);
    }

    istrm >> json_object;
    if (!json_object.is_object()) {
        LOGERROR("Could not parse file: {}", filename);
        return std::make_error_condition(std::errc::invalid_argument);
    }
    return std::error_condition();
}

std::error_condition loadConfigFile(json& config_map, nuraft_mesg::group_id_t const& _group_id, int32_t const _srv_id) {
    auto const config_file = STATE_PATH(_group_id, _srv_id, "/config.json");
    return jsonObjectFromFile(config_file, config_map);
}

std::error_condition loadStateFile(json& state_map, nuraft_mesg::group_id_t const& _group_id, int32_t const _srv_id) {
    auto const state_file = STATE_PATH(_group_id, _srv_id, "/state.json");
    return jsonObjectFromFile(state_file, state_map);
}

nuraft::ptr< nuraft::srv_config > fromServer(json const& server) {
    auto const id = static_cast< int32_t >(server["id"]);
    auto const dc_id = static_cast< int32_t >(server["dc_id"]);
    auto const endpoint = server["endpoint"];
    auto const aux = server["aux"];
    auto const learner = server["learner"];
    auto const prior = static_cast< int32_t >(server["priority"]);
    return nuraft::cs_new< nuraft::srv_config >(id, dc_id, endpoint, aux, learner, prior);
}

void fromServers(json const& servers, std::list< nuraft::ptr< nuraft::srv_config > >& server_list) {
    for (auto const& server_conf : servers) {
        server_list.push_back(fromServer(server_conf));
    }
}

json toServers(std::list< nuraft::ptr< nuraft::srv_config > > const& server_list) {
    auto servers = json::array();
    for (auto const& server_conf : server_list) {
        servers.push_back(json{{"id", server_conf->get_id()},
                               {"dc_id", server_conf->get_dc_id()},
                               {"endpoint", server_conf->get_endpoint()},
                               {"aux", server_conf->get_aux()},
                               {"learner", server_conf->is_learner()},
                               {"priority", server_conf->get_priority()}});
    }
    return servers;
}

nuraft::ptr< nuraft::cluster_config > fromClusterConfig(json const& cluster_config) {
    auto const& log_idx = cluster_config["log_idx"];
    auto const& prev_log_idx = cluster_config["prev_log_idx"];
    auto const& eventual = cluster_config["eventual_consistency"];

    auto raft_config = nuraft::cs_new< nuraft::cluster_config >(log_idx, prev_log_idx, eventual);
    fromServers(cluster_config["servers"], raft_config->get_servers());
    return raft_config;
}

test_state_mgr::test_state_mgr(int32_t srv_id, nuraft_mesg::peer_id_t const& srv_addr,
                               nuraft_mesg::group_id_t const& group_id) :
        nuraft_mesg::mesg_state_mgr(),
        _srv_id(srv_id),
        _srv_addr(srv_addr),
        _group_id(group_id),
        _state_machine(std::make_shared< test_state_machine >()) {}

nuraft::ptr< nuraft::cluster_config > test_state_mgr::load_config() {
    LOGDEBUG("Loading config for [{}]", _group_id);
    json config_map;
    if (auto err = loadConfigFile(config_map, _group_id, _srv_id); !err) { return fromClusterConfig(config_map); }
    auto conf = nuraft::cs_new< nuraft::cluster_config >();
    conf->get_servers().push_back(nuraft::cs_new< nuraft::srv_config >(_srv_id, 0, to_string(_srv_addr), "", false, 100));
    return conf;
}

nuraft::ptr< nuraft::log_store > test_state_mgr::load_log_store() {
    return nuraft::cs_new< nuraft::jungle_log_store >(STATE_PATH(_group_id, _srv_id, ""));
}

nuraft::ptr< nuraft::srv_state > test_state_mgr::read_state() {
    LOGDEBUG("Loading state for server: {}", _srv_id);
    json state_map;
    auto state = nuraft::cs_new< nuraft::srv_state >();
    if (auto err = loadStateFile(state_map, _group_id, _srv_id); !err) {
        try {
            state->set_term(static_cast< uint64_t >(state_map["term"]));
            state->set_voted_for(static_cast< int >(state_map["voted_for"]));
        } catch (std::out_of_range& e) { LOGWARN("State file was not in the expected format!"); }
    }
    return state;
}

void test_state_mgr::save_config(const nuraft::cluster_config& config) {
    auto const config_file = STATE_PATH(_group_id, _srv_id, "/config.json");
    auto json_obj = json{{"log_idx", config.get_log_idx()},
                         {"prev_log_idx", config.get_prev_log_idx()},
                         {"eventual_consistency", config.is_async_replication()},
                         {"user_ctx", config.get_user_ctx()},
                         {"servers", toServers(const_cast< nuraft::cluster_config& >(config).get_servers())}};
    try {
        std::ofstream ostrm(config_file, std::ios::binary);
        if (ostrm.is_open()) { ostrm << json_obj; }
    } catch (std::exception& e) { LOGERROR("Failed to write config values: {}", e.what()); }
}

void test_state_mgr::save_state(const nuraft::srv_state& state) {
    auto const state_file = STATE_PATH(_group_id, _srv_id, "/state.json");
    auto json_obj = json{{"term", state.get_term()}, {"voted_for", state.get_voted_for()}};

    try {
        std::ofstream ostrm(state_file, std::ios::binary);
        if (ostrm.is_open()) { ostrm << json_obj; }
    } catch (std::exception& e) { LOGERROR("Failed to write config values: {}", e.what()); }
}

uint32_t test_state_mgr::get_logstore_id() const { return 0; }

std::shared_ptr< nuraft::state_machine > test_state_mgr::get_state_machine() {
    return std::static_pointer_cast< nuraft::state_machine >(_state_machine);
}

test_state_mgr::~test_state_mgr() {
    if (auto path = std::filesystem::weakly_canonical(STATE_PATH(_group_id, _srv_id, ""));
        _will_destroy && std::filesystem::exists(path))
        std::filesystem::remove_all(path);
}

void test_state_mgr::permanent_destroy() { _will_destroy = true; }

void test_state_mgr::leave() {}

///// data service api helpers

nuraft_mesg::AsyncResult< sisl::GenericClientResponse >
test_state_mgr::data_service_request_bidirectional(nuraft_mesg::destination_t const& dest,
                                                   std::string const& request_name,
                                                   nuraft_mesg::io_blob_list_t const& cli_buf) {
    return m_repl_svc_ctx->data_service_request_bidirectional(dest, request_name, cli_buf);
}

nuraft_mesg::NullAsyncResult
test_state_mgr::data_service_request_unidirectional(nuraft_mesg::destination_t const& dest,
                                                    std::string const& request_name,
                                                    nuraft_mesg::io_blob_list_t const& cli_buf) {
    return m_repl_svc_ctx->data_service_request_unidirectional(dest, request_name, cli_buf);
}

bool test_state_mgr::register_data_service_apis(nuraft_mesg::Manager* messaging) {
    return messaging->bind_data_service_request(
               SEND_DATA, _group_id,
               [this](boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) {
                   rpc_data->set_comp_cb([this](boost::intrusive_ptr< sisl::GenericRpcData >&) { server_counter++; });
                   verify_data(rpc_data->request_blob());
                   m_repl_svc_ctx->send_data_service_response(nuraft_mesg::io_blob_list_t{rpc_data->request_blob()},
                                                              rpc_data);
               }) &&
        messaging->bind_data_service_request(
            REQUEST_DATA, _group_id, [this](boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) {
                rpc_data->set_comp_cb([this](boost::intrusive_ptr< sisl::GenericRpcData >&) { server_counter++; });
                m_repl_svc_ctx->send_data_service_response(nuraft_mesg::io_blob_list_t{rpc_data->request_blob()},
                                                           rpc_data);
            });
}

void test_state_mgr::verify_data(sisl::io_blob const& buf) {
    for (size_t read_sz{0}; read_sz < buf.size(); read_sz += sizeof(uint32_t)) {
        uint32_t const data{*reinterpret_cast< uint32_t* >(const_cast< uint8_t* >(buf.cbytes()) + read_sz)};
        EXPECT_EQ(data, data_vec[read_sz / sizeof(uint32_t)]);
    }
}

void test_state_mgr::fill_data_vec_big(nuraft_mesg::io_blob_list_t& cli_buf, uint32_t size_bytes) {
    auto cnt = size_bytes / sizeof(uint32_t);
    sisl::io_blob data(size_bytes);
    data_vec.clear();
    uint32_t* const write_buf{reinterpret_cast< uint32_t* >(data.bytes())};
    for (uint32_t i = 0; i < cnt; i++) {
        data_vec.emplace_back(i);
        write_buf[i] = data_vec.back();
    }
    cli_buf.emplace_back(data);
}

void test_state_mgr::fill_data_vec(nuraft_mesg::io_blob_list_t& cli_buf, uint32_t size_bytes) {
    auto cnt = size_bytes / sizeof(uint32_t);
    data_vec.clear();
    for (uint32_t i = 0; i < cnt; i++) {
        cli_buf.emplace_back(sizeof(uint32_t));
        uint32_t* const write_buf{reinterpret_cast< uint32_t* >(cli_buf[i].bytes())};
        data_vec.emplace_back(i);
        *write_buf = data_vec.back();
    }
}

uint16_t test_state_mgr::get_random_num() {
    static std::random_device dev;
    static std::mt19937 rng(dev());
    std::uniform_int_distribution< std::mt19937::result_type > dist(1001u, 65535u);
    return dist(rng);
}

uint32_t test_state_mgr::get_server_counter() { return server_counter.load(); }
