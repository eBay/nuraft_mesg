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

#include "grpc_server.hpp"
#include "raft_service.grpc.pb.h"

namespace raft_core {

using AsyncRaftSvc = RaftSvc::AsyncService;

class simple_server : public grpc_service<AsyncRaftSvc> {
    using sds::grpc::GrpcServer<AsyncRaftSvc>::completion_queue_;
    using sds::grpc::GrpcServer<AsyncRaftSvc>::service_;

    using grpc_service<AsyncRaftSvc>::grpc_service;

    void ready() override {
        (new sds::grpc::ServerCallData<AsyncRaftSvc, RaftMessage, RaftMessage>(
                &service_,
                completion_queue_.get(),
                "Step",
                &AsyncRaftSvc::RequestStep,
                std::bind(&grpc_server::step, this, std::placeholders::_1)
                )
            )->proceed();
    }
};

}
