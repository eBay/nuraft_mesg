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
//   grpc_server step function and response transformations
//
#include "grpc_server.hpp"

namespace nuraft_mesg {

nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > grpc_server::add_srv(const nuraft::srv_config& cfg) {
    return _raft_server->add_srv(cfg);
}

void grpc_server::yield_leadership(bool immediate) { return _raft_server->yield_leadership(immediate, -1); }

bool grpc_server::request_leadership() { return _raft_server->request_leadership(); }

void grpc_server::get_srv_config_all(std::vector< nuraft::ptr< nuraft::srv_config > >& configs_out) {
    _raft_server->get_srv_config_all(configs_out);
}

nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > grpc_server::rem_srv(int const member_id) {
    return _raft_server->remove_srv(member_id);
}

nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > >
grpc_server::append_entries(std::vector< nuraft::ptr< nuraft::buffer > > const& logs) {
    return _raft_server->append_entries(logs);
}

} // namespace nuraft_mesg
