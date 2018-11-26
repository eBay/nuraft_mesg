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
#include <sds_grpc/server.h>

#include "utils.hpp"

namespace raft_core {

class grpc_server {
    shared<cstn::raft_server> _raft_server;

 public:
    explicit grpc_server(shared<cstn::raft_server>&& raft_server) :
        _raft_server(std::move(raft_server))
     { }

    ~grpc_server() { if (_raft_server) _raft_server->shutdown(); }

    RaftMessage step(RaftMessage const& request) {
        LOGTRACEMOD(raft_core, "Stepping [{}] from: [{}] to: [{}]",
            cstn::msg_type_to_string(cstn::msg_type(request.base().type())),
            request.base().src(),
            request.base().dest()
            );
        auto rcreq = toRequest(request);
        auto resp = _raft_server->process_req(*rcreq);
        assert(resp);
        return fromResponse(*resp);
    }
};

template<typename TSERVICE>
class grpc_service :
  public grpc_server,
  public sds::grpc::GrpcServer<TSERVICE>
{
 public:
    explicit grpc_service(shared<cstn::raft_server>&& raft_server) :
        grpc_server(std::move(raft_server)),
        sds::grpc::GrpcServer<TSERVICE>()
    { }

    void process(sds::grpc::ServerCallMethod* cm) override { cm->proceed(); }
};

}
