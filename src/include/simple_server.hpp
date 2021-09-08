///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Defines a simple RAFT-over-gRPC service that is defined in
// raft_service.proto and can be used directly for simple RAFT-to-RAFT
// transports that do not need to expand upon the messages or RPC itself.
//

#pragma once

#include <grpc_helper/rpc_server.hpp>

#include "grpc_server.hpp"
#include "raft_service.grpc.pb.h"

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

    void associate(grpc_helper::GrpcServer* server) override {
        assert(server);
        if (!server->register_async_service< RaftSvc >()) {
            LOGERRORMOD(nuraft, "Could not register RaftSvc with gRPC!");
            abort();
        }
    }

    void bind(grpc_helper::GrpcServer* server) override {
        assert(server);
        if (!server->register_rpc< RaftSvc, RaftMessage, RaftMessage, false >(
                "Simple", &AsyncRaftSvc::RequestStep,
                [this](const grpc_helper::AsyncRpcDataPtr< RaftSvc, RaftMessage, RaftMessage >& rpc_data) -> bool {
                    this->step(rpc_data->request(), rpc_data->response());
                    return true;
                })) {
            LOGERRORMOD(nuraft, "Could not bind gRPC ::Step to routine!");
            abort();
        }
    }
};

} // namespace nuraft_grpc
