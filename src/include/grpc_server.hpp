/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/

// Brief:
//   Translates and forwards the gRPC Step() to cornerstone's raft_server::step()
//
#pragma once

#include "common.hpp"

#include <libnuraft/raft_server_handler.hxx>
#include <grpcpp/server.h>

namespace sisl {
class GrpcServer;
}

namespace nuraft_mesg {

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

    void get_srv_config_all(std::vector< nuraft::ptr< nuraft::srv_config > >& configs_out);

    nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > >
    append_entries(const std::vector< nuraft::ptr< nuraft::buffer > >& logs);

    ::grpc::Status step(const RaftMessage& request, RaftMessage& reply);
    shared< nuraft::raft_server > raft_server() { return _raft_server; }

    // Setup the RPC call backs
    virtual void associate(sisl::GrpcServer* server) = 0;
    virtual void bind(sisl::GrpcServer* server) = 0;
};

} // namespace nuraft_mesg
