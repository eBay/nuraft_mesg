#pragma once

#include <cornerstone.hxx>
#include <jungle_log_store.h>
#include <picojson/picojson.h>
#include <sds_logging/logging.h>

namespace cs = ::cornerstone;

class simple_state_mgr : public cs::state_mgr {
 public:
   explicit simple_state_mgr(int32_t srv_id);

   cs::ptr<cs::cluster_config> load_config() override;

   void save_config(const cs::cluster_config& config) override;
   void save_state(const cs::srv_state& state) override;
   cs::ptr<cs::srv_state> read_state() override;
   cs::ptr<cs::log_store> load_log_store() override {
       return cs::cs_new<cs::jungle_log_store>(format(fmt("store{}"), _srv_id));
   }
   int32_t server_id() override { return _srv_id; }

   void system_exit(const int exit_code) override
   { LOGINFO("System exiting with code [{}]", exit_code); }

 private:
   int32_t const _srv_id;
};
