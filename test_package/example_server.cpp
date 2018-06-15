/**
*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  The ASF licenses
* this file to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "cornerstone/raft_core_grpc.hpp"
#include <iostream>
#include <cassert>
#include <stdlib.h>
#include <grpcpp/server.h>
#include <grpcpp/security/server_credentials.h>
#include <sds_logging/logging.h>

using namespace cornerstone;

SDS_LOGGING_INIT

std::condition_variable stop_cv;
std::mutex stop_cv_lock;

struct sds_logger : ::cornerstone::logger {
    void debug(const std::string& log_line) override {
        LOGDEBUG("{}", log_line);
    }

    void info(const std::string& log_line) override {
        LOGINFO("{}", log_line);
    }

    void warn(const std::string& log_line) override {
        LOGWARN("{}", log_line);
    }

    void err(const std::string& log_line) override {
       LOGERROR("{}", log_line);
    }

    void set_level(int l) override {
        spdlog::set_level((spdlog::level::level_enum)l);
    }
};

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

class echo_state_machine : public state_machine {
public:
    echo_state_machine() : lock_(), last_commit_idx_(0) {}
public:
    virtual ptr<buffer> commit(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        std::cout << "commit message [" << log_idx << "]"
                  << ": " << reinterpret_cast<const char*>(data.data()) << std::endl;
        last_commit_idx_ = log_idx;
        return nullptr;
    }

    virtual ptr<buffer> pre_commit(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        std::cout << "pre-commit [" << log_idx << "]"
                  << ": " << reinterpret_cast<const char*>(data.data()) << std::endl;
        return nullptr;
    }

    virtual void rollback(const ulong log_idx, buffer& data) {
        auto_lock(lock_);
        std::cout << "rollback [" << log_idx << "]"
                  << ": " << reinterpret_cast<const char*>(data.data()) << std::endl;
    }

    virtual void save_snapshot_data(snapshot& s, const ulong offset, buffer& data) {}
    virtual bool apply_snapshot(snapshot& s) {
        return true;
    }

    virtual int read_snapshot_data(snapshot& s, const ulong offset, buffer& data) {
        return 0;
    }

    virtual ptr<snapshot> last_snapshot() {
        return ptr<snapshot>();
    }

    virtual void create_snapshot(snapshot& s, async_result<bool>::handler_type& when_done) {}

    virtual ulong last_commit_index() { return last_commit_idx_; }

private:
    std::mutex lock_;
    ulong last_commit_idx_;
};

void run_echo_server(int srv_id) {
    // State manager (RAFT log store, config).
    ptr<state_mgr> smgr(cs_new<simple_state_mgr>(srv_id));

    // State machine.
    ptr<state_machine> smachine(cs_new<echo_state_machine>());

    // Parameters.
    raft_params* params(new raft_params());
    (*params).with_election_timeout_lower(200)
             .with_election_timeout_upper(400)
             .with_hb_interval(100)
             .with_max_append_size(100)
             .with_rpc_failure_backoff(50);

    // gRPC service.
    ptr<grpc_service> grpc_svc_(cs_new<grpc_service>());
    sds_logging::SetLogger(spdlog::stdout_color_mt("raft_member"));
    ptr<logger> l = std::make_shared<sds_logger>();
    ptr<rpc_client_factory> rpc_cli_factory = grpc_svc_;

    ptr<asio_service> asio_svc_(cs_new<asio_service>());
    ptr<delayed_task_scheduler> scheduler = asio_svc_;

    ptr<rpc_listener> listener;
    ::grpc::ServerBuilder builder;
    std::string server_address = std::string("0.0.0.0:") + std::to_string(9000 + srv_id);
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(grpc_svc_.get());

    // Run server.
    context* ctx(new context(smgr,
                             smachine,
                             listener,
                             l,
                             rpc_cli_factory,
                             scheduler,
                             params));
    ptr<raft_server> server(cs_new<raft_server>(ctx));
    grpc_svc_->registerRaftCore(server);
    std::unique_ptr<::grpc::Server> grpc_server(builder.BuildAndStart());

    {
        std::unique_lock<std::mutex> ulock(stop_cv_lock);
        stop_cv.wait(ulock);
        grpc_svc_.reset();
    }
}

int main(int argc, char** argv) {
    if (argc == 2) {
        int srv_id = atoi(argv[1]);
        if (srv_id >=1 && srv_id <= 3) {
            run_echo_server(srv_id);
            return 0;
        }
    }
    printf("usage: server <ID>\n");
    printf("    ID          server ID, 1 -- 3.\n");

    return 0;
}
