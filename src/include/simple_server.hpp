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

// Brief:
//   Defines a simple RAFT-over-gRPC service that is defined in
// raft_service.proto and can be used directly for simple RAFT-to-RAFT
// transports that do not need to expand upon the messages or RPC itself.
//
#pragma once

#include <sisl/grpc/rpc_server.hpp>

#include "grpc_server.hpp"
#include "proto/raft_service.grpc.pb.h"

namespace nuraft_grpc {

using AsyncRaftSvc = RaftSvc::AsyncService;

class simple_server : public grpc_server {
public:
    using grpc_server::grpc_server;

    ~simple_server() override {
        if (auto srv = raft_server(); srv) {
            srv->stop_server();
            srv->shutdown();
        }
    }

    void associate(sisl::GrpcServer* server) override {
        assert(server);
        if (!server->register_async_service< RaftSvc >()) {
            LOGERRORMOD(nuraft, "Could not register RaftSvc with gRPC!");
            abort();
        }
    }

    void bind(sisl::GrpcServer* server) override {
        assert(server);
        if (!server->register_rpc< RaftSvc, RaftMessage, RaftMessage, false >(
                "Simple", &AsyncRaftSvc::RequestStep,
                [this](const sisl::AsyncRpcDataPtr< RaftSvc, RaftMessage, RaftMessage >& rpc_data) -> bool {
                    this->step(rpc_data->request(), rpc_data->response());
                    return true;
                })) {
            LOGERRORMOD(nuraft, "Could not bind gRPC ::Step to routine!");
            abort();
        }
    }
};

} // namespace nuraft_grpc
