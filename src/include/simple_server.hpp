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

#include <sds_grpc/server.h>

#include "grpc_server.hpp"
#include "raft_service.grpc.pb.h"

namespace sds {

using AsyncRaftSvc = RaftSvc::AsyncService;

class simple_server : public grpc_server {
 public:
    using grpc_server::grpc_server;

    void associate(sds::grpc::GrpcServer* server) override {
        assert(server);
        if (!server->register_async_service<RaftSvc>()) {
            LOGERRORMOD(nuraft, "Could not register RaftSvc with gRPC!");
            abort();
        }
    }

    void bind(sds::grpc::GrpcServer* server) override {
        assert(server);
        if (!server->register_rpc<RaftSvc, RaftMessage, RaftMessage>(
            &AsyncRaftSvc::RequestStep,
            std::bind(&grpc_server::step, this, std::placeholders::_1, std::placeholders::_2))) {
            LOGERRORMOD(nuraft, "Could not bind gRPC ::Step to routine!");
            abort();
        }
    }
};

}
