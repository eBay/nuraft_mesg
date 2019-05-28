///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Defines a simple RAFT-over-gRPC client for the service that is defined
// in raft_service.proto and can be used directly for simple RAFT-to-RAFT
// transports that do not need to expand upon the messages or RPC itself.
//
#pragma once

#include "grpc_client.hpp"
#include "raft_service.grpc.pb.h"

namespace sds {

class simple_grpc_client :
    public grpc_client<RaftSvc>
{
 public:
    using grpc_client<RaftSvc>::grpc_client;

 protected:
    void send(RaftMessage const &message, handle_resp complete) override {
        _stub->call_unary<RaftMessage, RaftMessage>(message,
                                                    &RaftSvc::StubInterface::AsyncStep,
                                                    complete);
    }
};

}
