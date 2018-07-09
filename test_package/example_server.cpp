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

#include <iostream>
#include <cassert>
#include <cxxopts/cxxopts.hpp>
#include <grpcpp/server.h>
#include <grpcpp/security/server_credentials.h>

#include "cornerstone/raft_core_grpc.hpp"
#include "example_service.grpc.pb.h"

#include "example_factory.h"
#include "example_logger.h"
#include "example_state_manager.h"
#include "example_state_machine.h"


std::condition_variable stop_cv;
std::mutex stop_cv_lock;

struct example_service :
      public cornerstone::grpc_service,
      public raft_core::ExampleSvc::Service {
   ::grpc::Status Step(::grpc::ServerContext *context,
                       ::raft_core::RaftMessage const *request,
                       ::raft_core::RaftMessage *response) override {
      return step(context, request, response);
   }
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
    ptr<example_service> grpc_svc_(cs_new<example_service>());
    sds_logging::SetLogger(spdlog::stdout_color_mt("raft_member"));
    ptr<logger> l = std::make_shared<sds_logger>();
    ptr<rpc_client_factory> rpc_cli_factory = std::make_shared<example_factory>();

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
    auto server_id {0u};

    cxxopts::Options options(argv[0], "Raft Server");
    options.add_options()
          ("h,help", "Help message")
          ("log_level", "Log level (0-5) def:1", cxxopts::value<uint32_t>(), "level")
          ("server_id", "Servers ID (1-3)", cxxopts::value<uint32_t>(server_id));
    options.parse_positional("server_id");

    options.parse(argc, argv);

    if (options.count("help")) {
        std::cout << options.help({}) << std::endl;
        return 0;
    }

    auto log_level = spdlog::level::level_enum::debug;
    if (options.count("log_level")) {
        log_level = (spdlog::level::level_enum)options["log_level"].as<uint32_t>();
    }
    if (spdlog::level::level_enum::off < log_level) {
        std::cout << "LogLevel must be between 0 and 5." << std::endl;
        std::cout << options.help({}) << std::endl;
        return -1;
    }

    // Can start using LOG from this point onward.
    sds_logging::SetLogger(spdlog::stdout_color_mt("raft_client"), log_level);
    spdlog::set_pattern("[%D %H:%M:%S] [%l] [%t] %v");

    if (0 < server_id && 4 > server_id) {
         run_echo_server(server_id);
    } else {
        LOGERROR("Server ID must be between 1 and 3");
    }
    return 0;
}
