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

#include "common.hpp"

#include <libnuraft/raft_server_handler.hxx>
#include <grpcpp/server.h>

namespace grpc_helper {
class GrpcServer;
}

namespace nuraft_grpc {

class grpc_server : public nuraft::raft_server_handler {
    shared< nuraft::raft_server > _raft_server;

public:
    explicit grpc_server(shared< nuraft::raft_server >& raft_server) : _raft_server(raft_server) {}
    virtual ~grpc_server() = default;
    grpc_server(const grpc_server&) = delete;
    grpc_server& operator=(const grpc_server&) = delete;

    nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > add_srv(const nuraft::srv_config& cfg);

    nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > rem_srv(int const member_id);

    bool request_leadership();
    void yield_leadership(bool immediate = false);

    nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > >
    append_entries(const std::vector< nuraft::ptr< nuraft::buffer > >& logs);

    ::grpc::Status step(const RaftMessage& request, RaftMessage& reply);
    shared< nuraft::raft_server > raft_server() { return _raft_server; }

    // Setup the RPC call backs
    virtual void associate(grpc_helper::GrpcServer* server) = 0;
    virtual void bind(grpc_helper::GrpcServer* server) = 0;
};

} // namespace nuraft_grpc
