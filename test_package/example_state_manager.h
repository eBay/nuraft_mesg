#pragma once

#include <libnuraft/nuraft.hxx>
#include <sisl/logging/logging.h>

class simple_state_mgr : public nuraft::state_mgr {
public:
    explicit simple_state_mgr(int32_t srv_id);

    nuraft::ptr< nuraft::cluster_config > load_config() override;
    void save_config(const nuraft::cluster_config& config) override;
    void save_state(const nuraft::srv_state& state) override;
    nuraft::ptr< nuraft::srv_state > read_state() override;
    nuraft::ptr< nuraft::log_store > load_log_store() override;
    int32_t server_id() override { return _srv_id; }

    void system_exit(const int exit_code) override { LOGINFO("System exiting with code [{}]", exit_code); }

private:
    int32_t const _srv_id;
};
