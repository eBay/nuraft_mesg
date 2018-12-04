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

constexpr char worker_name[] = "simple_raft_client";

class simple_grpc_client :
    public grpc_client,
    public sds::grpc::GrpcAsyncClient
{
    ::sds::grpc::GrpcAsyncClient::AsyncStub<RaftSvc>::UPtr _stub;

 public:
    simple_grpc_client(std::string const& addr,
                       std::string const& target_domain,
                       std::string const& ssl_cert) :
        grpc_client::grpc_client(),
        sds::grpc::GrpcAsyncClient(addr, target_domain, ssl_cert)
    {
        assert(sds::grpc::GrpcAyncClientWorker::create_worker(worker_name, 2));
    }

    bool init() override {
        if (!sds::grpc::GrpcAsyncClient::init()) {
            LOGERROR("Initializing client failed!");
            return false;
        }
        _stub = sds::grpc::GrpcAsyncClient::make_stub<RaftSvc>(worker_name);
        if (!_stub)
            LOGERROR("Failed to create Async client!");
        else
            LOGDEBUG("Created Async client.");
        return (!!_stub);
    }

 protected:
    void send(RaftMessage const &message, handle_resp complete) override {
        _stub->call_unary<RaftMessage, RaftMessage>(message,
                                                    &RaftSvc::StubInterface::AsyncStep,
                                                    complete);
    }
};

}
