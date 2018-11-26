///
// Copyright 2018 (c) eBay Corporation
//
#include "example_state_manager.h"

std::error_condition
jsonObjectFromFile(std::ifstream& istrm, picojson::object& json_object) {
   assert(istrm.is_open());

   std::string err;
   picojson::value object;
   picojson::parse(object,
                   std::istream_iterator<char>(istrm),
                   std::istream_iterator<char>(),
                   &err);

   if (!err.empty() || !object.is<picojson::object>()) {
      LOGERROR("Could not parse file: {}", err);
      return std::make_error_condition(std::errc::invalid_argument);
   }
   json_object = object.get<picojson::object>();
   return std::error_condition();
}

std::error_condition
loadConfigFile(picojson::object& config_map, int32_t const _srv_id) {
   auto const config_file = format(FMT_STRING("s{}.config"), _srv_id);
   std::ifstream istrm(config_file, std::ios::binary);
   if (!istrm.is_open()) {
      return std::make_error_condition(std::errc::no_such_file_or_directory);
   }

   return jsonObjectFromFile(istrm, config_map);
}

std::error_condition
loadStateFile(picojson::object& state_map, int32_t const _srv_id) {
   auto const state_file = format(FMT_STRING("s{}.state"), _srv_id);
   std::ifstream istrm(state_file, std::ios::binary);
   if (!istrm.is_open()) {
      return std::make_error_condition(std::errc::no_such_file_or_directory);
   }

   return jsonObjectFromFile(istrm, state_map);
}

cs::ptr<cs::srv_config>
fromServer(picojson::object const& server) {
   int32_t const id = server.at("id").get<double>();
   int32_t const dc_id = server.at("dc_id").get<double>();
   auto const endpoint = server.at("endpoint").get<std::string>();
   auto const aux = server.at("aux").get<std::string>();
   auto const learner = server.at("learner").get<bool>();
   int32_t const prior = server.at("priority").get<double>();
   return cs::cs_new<cs::srv_config>(id, dc_id, endpoint, aux, learner, prior);
}

picojson::value
toServer(cs::ptr<cs::srv_config> const& server_conf) {
   picojson::object server;
   server["id"] = picojson::value((double)server_conf->get_id());;
   server["dc_id"] = picojson::value((double)server_conf->get_dc_id());
   server["endpoint"] = picojson::value(server_conf->get_endpoint());
   server["aux"] = picojson::value(server_conf->get_aux());
   server["learner"] = picojson::value(server_conf->is_learner());
   server["priority"] = picojson::value((double)server_conf->get_priority());
   return picojson::value(server);
}

void
fromServers(picojson::array const& servers,
            std::list<cs::ptr<cs::srv_config>>& server_list) {
   for (auto const& server_conf : servers) {
      server_list.push_back(fromServer(server_conf.get<picojson::object>()));
   }
}

picojson::value
toServers(std::list<cs::ptr<cs::srv_config>> const& server_list) {
   picojson::array servers;
   for (auto const& server_conf : server_list) {
      servers.push_back(toServer(server_conf));
   }
   return picojson::value(servers);
}

cs::ptr<cs::cluster_config>
fromClusterConfig(picojson::object const& cluster_config) {
   auto const log_idx = cluster_config.at("log_idx").get<double>();
   auto const prev_log_idx = cluster_config.at("prev_log_idx").get<double>();
   auto const eventual = cluster_config.at("eventual_consistency").get<bool>();

   auto raft_config = cs::cs_new<cs::cluster_config>(log_idx, prev_log_idx, eventual);
   fromServers(cluster_config.at("servers").get<picojson::array>(), raft_config->get_servers());
   return raft_config;
}

picojson::value
toClusterConfig(cs::cluster_config const& config) {
   picojson::object cluster_config;
   cluster_config["log_idx"] = picojson::value((double)config.get_log_idx());
   cluster_config["prev_log_idx"] = picojson::value((double)config.get_prev_log_idx());
   cluster_config["eventual_consistency"] = picojson::value(config.is_eventual_consistency());
   cluster_config["user_ctx"] = picojson::value(config.get_user_ctx());

   cluster_config["servers"] = toServers(const_cast<cs::cluster_config&>(config).get_servers());

   return picojson::value(cluster_config);
}

simple_state_mgr::simple_state_mgr(int32_t srv_id)
   : cs::state_mgr(),
     _srv_id(srv_id)
{ }

cs::ptr<cs::cluster_config>
simple_state_mgr::load_config() {
   LOGDEBUG("Loading config for", _srv_id);
   picojson::object config_map;
   if (auto err = loadConfigFile(config_map, _srv_id); !err) {
      return fromClusterConfig(config_map);
   }
   auto conf = cs::cs_new<cs::cluster_config>();
   conf->get_servers().push_back(cs::cs_new<cs::srv_config>(_srv_id, std::to_string(_srv_id)));
   return conf;
}

cs::ptr<cs::srv_state>
simple_state_mgr::read_state() {
   LOGDEBUG("Loading state for server: {}", _srv_id);
   picojson::object state_map;
   auto state = cs::cs_new<cs::srv_state>();
   if (auto err = loadStateFile(state_map, _srv_id); !err) {
      try {
         state->set_term((uint64_t) state_map.at("term").get<double>());
         state->set_voted_for((int) state_map.at("voted_for").get<double>());
      } catch (std::out_of_range& e) {
         LOGWARN("State file was not in the expected format!");
      }
   }
   return state;
}

void
simple_state_mgr::save_config(const cs::cluster_config& config) {
   auto const config_file = format(FMT_STRING("s{}.config"), _srv_id);
   auto json_obj = toClusterConfig(config);
   try {
      std::ofstream ostrm(config_file, std::ios::binary);
      if (ostrm.is_open()) {
         ostrm << json_obj.serialize();
      }
   } catch (std::exception &e) {
      LOGERROR("Failed to write config values: {}", e.what());
   }
}

void
simple_state_mgr::save_state(const cs::srv_state& state) {
   auto const state_file = format(FMT_STRING("s{}.state"), _srv_id);
   picojson::object json_obj;
   json_obj["term"] = picojson::value((double)state.get_term());
   json_obj["voted_for"] = picojson::value((double)state.get_voted_for());

   try {
      std::ofstream ostrm(state_file, std::ios::binary);
      if (ostrm.is_open()) {
         ostrm << picojson::value(json_obj).serialize();
      }
   } catch (std::exception &e) {
      LOGERROR("Failed to write config values: {}", e.what());
   }
}
