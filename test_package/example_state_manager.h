#pragma once

#include <nupillar/nupillar.hxx>
#include <sds_logging/logging.h>

class simple_state_mgr : public nupillar::state_mgr {
 public:
   explicit simple_state_mgr(int32_t srv_id);

   nupillar::ptr<nupillar::cluster_config> load_config() override;
   void save_config(const nupillar::cluster_config& config) override;
   void save_state(const nupillar::srv_state& state) override;
   nupillar::ptr<nupillar::srv_state> read_state() override;
   nupillar::ptr<nupillar::log_store> load_log_store() override;
   int32_t server_id() override { return _srv_id; }

   void system_exit(const int exit_code) override
   { LOGINFO("System exiting with code [{}]", exit_code); }

 private:
   int32_t const _srv_id;
};
