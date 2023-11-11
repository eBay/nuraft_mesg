#pragma once

#include <libnuraft/raft_server_handler.hxx>
#include <grpcpp/server.h>

namespace sisl {
class GrpcServer;
}

namespace nuraft_mesg {

// Brief:
//   Translates and forwards the gRPC Step() to cornerstone's raft_server::step()
//
class grpc_server {
    std::shared_ptr< nuraft::raft_server > _raft_server;

public:
    explicit grpc_server(std::shared_ptr< nuraft::raft_server >& raft_server) : _raft_server(raft_server) {}
    virtual ~grpc_server() = default;
    grpc_server(const grpc_server&) = delete;
    grpc_server& operator=(const grpc_server&) = delete;

    nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > add_srv(const nuraft::srv_config& cfg);

    nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > rem_srv(int const member_id);

    bool request_leadership();
    void yield_leadership(bool immediate = false);

    void get_srv_config_all(std::vector< nuraft::ptr< nuraft::srv_config > >& configs_out);

    nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > >
    append_entries(const std::vector< nuraft::ptr< nuraft::buffer > >& logs);

    std::shared_ptr< nuraft::raft_server > raft_server() { return _raft_server; }
};

} // namespace nuraft_mesg
