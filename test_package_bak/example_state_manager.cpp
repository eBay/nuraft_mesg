#include "example_state_manager.h"
#include "example_state_machine.h"

#include <fstream>

#include <jungle_log_store.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

std::error_condition loadConfigFile(json& config_map, std::string const& _group_id, int32_t const _srv_id) {
    auto const config_file = fmt::format(FMT_STRING("{}_s{}/config.json"), _group_id, _srv_id);
    return jsonObjectFromFile(config_file, config_map);
}

std::error_condition loadStateFile(json& state_map, std::string const& _group_id, int32_t const _srv_id) {
    auto const state_file = fmt::format(FMT_STRING("{}_s{}/state.json"), _group_id, _srv_id);
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

simple_state_mgr::simple_state_mgr(int32_t srv_id, std::string const& srv_addr, std::string const& group_id) :
        nuraft_mesg::mesg_state_mgr(), _srv_id(srv_id), _srv_addr(srv_addr), _group_id(group_id.c_str()) {}

nuraft::ptr< nuraft::cluster_config > simple_state_mgr::load_config() {
    LOGDEBUG("Loading config for [{}]", _group_id);
    json config_map;
    if (auto err = loadConfigFile(config_map, _group_id, _srv_id); !err) { return fromClusterConfig(config_map); }
    auto conf = nuraft::cs_new< nuraft::cluster_config >();
    conf->get_servers().push_back(nuraft::cs_new< nuraft::srv_config >(_srv_id, _srv_addr));
    return conf;
}

nuraft::ptr< nuraft::log_store > simple_state_mgr::load_log_store() {
    return nuraft::cs_new< nuraft::jungle_log_store >(fmt::format(FMT_STRING("{}_s{}"), _group_id, _srv_id));
}

nuraft::ptr< nuraft::srv_state > simple_state_mgr::read_state() {
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

void simple_state_mgr::save_config(const nuraft::cluster_config& config) {
    auto const config_file = fmt::format(FMT_STRING("{}_s{}/config.json"), _group_id, _srv_id);
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

void simple_state_mgr::save_state(const nuraft::srv_state& state) {
    auto const state_file = fmt::format(FMT_STRING("{}_s{}/state.json"), _group_id, _srv_id);
    auto json_obj = json{{"term", state.get_term()}, {"voted_for", state.get_voted_for()}};

    try {
        std::ofstream ostrm(state_file, std::ios::binary);
        if (ostrm.is_open()) { ostrm << json_obj; }
    } catch (std::exception& e) { LOGERROR("Failed to write config values: {}", e.what()); }
}

uint32_t simple_state_mgr::get_logstore_id() const { return 0; }

std::shared_ptr< nuraft::state_machine > simple_state_mgr::get_state_machine() {
    return std::make_shared<echo_state_machine>();
}

void simple_state_mgr::permanent_destroy() {}

void simple_state_mgr::leave() {}
