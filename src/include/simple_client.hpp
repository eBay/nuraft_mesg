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

namespace raft_core {

class simple_grpc_client :
    public grpc_client<RaftSvc>
{
 public:
     using sds::grpc::GrpcConnection<RaftSvc>::dead_line_;
     using sds::grpc::GrpcConnection<RaftSvc>::completion_queue_;

     using grpc_client<RaftSvc>::grpc_client;
     ~simple_grpc_client() override = default;

 protected:
     void send(RaftMessage const &message, handle_resp complete) override {
         auto call = new sds::grpc::ClientCallData<RaftMessage, RaftMessage>(complete);
         call->set_deadline(dead_line_);
         call->responder_reader() = stub()->AsyncStep(&call->context(), message, completion_queue_);
         call->responder_reader()->Finish(&call->reply(), &call->status(), (void*)call);
     }
};

}
