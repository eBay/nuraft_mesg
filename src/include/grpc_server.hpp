///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Translates and forwards the gRPC Step() to cornerstone's raft_server::step()
//

#pragma once

#include <cornerstone.hxx>
#include <grpcpp/server.h>

#include "common.hpp"

namespace sds::grpc {
class GrpcServer;
}

namespace raft_core {

class grpc_server {
    shared<cstn::raft_server> _raft_server;

 public:
    explicit grpc_server(shared<cstn::raft_server>& raft_server) :
        _raft_server(raft_server)
     { }
    virtual ~grpc_server() = default;
    grpc_server(const grpc_server&) = delete;
    grpc_server& operator=(const grpc_server&) = delete;

    ::grpc::Status step(RaftMessage& request, RaftMessage& reply);

    // Setup the RPC call backs
    virtual void associate(sds::grpc::GrpcServer* server) = 0;
    virtual void bind(sds::grpc::GrpcServer* server) = 0;
};

}
