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

#include <cassert>
#include <grpcpp/server.h>
#include <grpcpp/security/server_credentials.h>

#include <cornerstone/raft_core_grpc.hpp>
#include <sds_logging/logging.h>
#include "example_service.grpc.pb.h"

#include "example_factory.h"
#include "example_logger.h"
#include "example_state_machine.h"
#include "example_state_manager.h"

using namespace cornerstone;

SDS_OPTION_GROUP(server, (server_id, "", "server_id", "Servers ID (0-9)", cxxopts::value<uint32_t>(), ""))

SDS_OPTIONS_ENABLE(logging, server)
SDS_LOGGING_INIT()

struct example_service :
      public cornerstone::grpc_service,
      public raft_core::ExampleSvc::Service {

   example_service(ptr<raft_server>&& raft_server) :
       cornerstone::grpc_service(std::move(raft_server))
   {}

   ::grpc::Status Step(::grpc::ServerContext *context,
                       ::raft_core::RaftMessage const *request,
                       ::raft_core::RaftMessage *response) override {
      return step(context, request, response);
   }
};

int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, logging, server);
    auto server_id = SDS_OPTIONS["server_id"].as<uint32_t>();

    // Can start using LOG from this point onward.
    sds_logging::SetLogger(format(fmt("server_{}"), server_id));
    spdlog::set_pattern("[%D %T] [%^%l%$] [%n] [%t] %v");

    if (0 <= server_id && 10 > server_id) {
       // State manager (RAFT log store, config).
       ptr<state_mgr> smgr = cs_new<simple_state_mgr>(server_id);

       // State machine.
       ptr<state_machine> smachine = cs_new<echo_state_machine>();

       // Parameters.
       raft_params* params(new raft_params());
       (*params).with_election_timeout_lower(200)
                .with_election_timeout_upper(400)
                .with_hb_interval(100)
                .with_max_append_size(100)
                .with_rpc_failure_backoff(50);

       ptr<logger> l = std::make_shared<sds_logger>();
       ptr<rpc_client_factory> rpc_cli_factory = std::make_shared<example_factory>();
       ptr<asio_service> asio_svc_ = cs_new<asio_service>();
       ptr<delayed_task_scheduler> scheduler = std::static_pointer_cast<delayed_task_scheduler>(asio_svc_);
       ptr<rpc_listener> listener;

       // Run server.
       ptr<raft_server> server = cs_new<raft_server>(new context(smgr,
             smachine,
             listener,
             l,
             rpc_cli_factory,
             scheduler,
             params));
       auto grpc_svc_ = cs_new<example_service>(std::move(server));

       ::grpc::ServerBuilder builder;
       auto server_address = format(fmt("0.0.0.0:900{}"), server_id);
       builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
       builder.RegisterService(grpc_svc_.get());
       auto grpc_server = builder.BuildAndStart();

        std::condition_variable stop_cv;
        std::mutex stop_cv_lock;
        {
            std::unique_lock<std::mutex> ulock(stop_cv_lock);
            stop_cv.wait(ulock);
            grpc_svc_.reset();
        }
    } else {
        LOGERROR("Server ID must be between 0 and 9");
    }
    return 0;
}
