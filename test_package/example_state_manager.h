#pragma once

#include "cornerstone/raft_core_grpc.hpp"

using namespace cornerstone;

class simple_state_mgr: public state_mgr{
public:
    simple_state_mgr(int32 srv_id)
        : srv_id_(srv_id) {
        store_path_ = sstrfmt("store%d").fmt(srv_id_);
    }

public:
    virtual ptr<cluster_config> load_config() {
        ptr<cluster_config> conf = cs_new<cluster_config>();
        conf->get_servers().push_back(cs_new<srv_config>(1, "127.0.0.1:9001"));
        conf->get_servers().push_back(cs_new<srv_config>(2, "127.0.0.1:9002"));
        conf->get_servers().push_back(cs_new<srv_config>(3, "127.0.0.1:9003"));
        return conf;
    }

    virtual void save_config(const cluster_config& config) {}
    virtual void save_state(const srv_state& state) {}
    virtual ptr<srv_state> read_state() {
        return cs_new<srv_state>();
    }

    virtual ptr<log_store> load_log_store() {
        return cs_new<fs_log_store>(store_path_);
    }

    virtual int32 server_id() {
        return srv_id_;
    }

    virtual void system_exit(const int exit_code) {
        std::cout << "system exiting with code " << exit_code << std::endl;
    }

private:
    int32 srv_id_;
    std::string store_path_;
};

