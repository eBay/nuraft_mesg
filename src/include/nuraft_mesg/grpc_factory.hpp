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
//   Implements cornerstone's rpc_client_factory providing sisl::GrpcAsyncClient
//   inherited rpc_client instances sharing a common worker pool.
#pragma once

#include <libnuraft/async.hxx>
#include <memory>

#include <folly/SharedMutex.h>
#include <libnuraft/nuraft.hxx>

#include "common.hpp"

namespace nuraft_mesg {

using client_factory_lock_type = folly::SharedMutex;

class grpc_factory : public nuraft::rpc_client_factory, public std::enable_shared_from_this< grpc_factory > {
    std::string _worker_name;

protected:
    client_factory_lock_type _client_lock;
    std::map< peer_id_t, std::shared_ptr< nuraft::rpc_client > > _clients;

public:
    grpc_factory(int const cli_thread_count, std::string const& name);
    ~grpc_factory() override = default;

    std::string const& workerName() const { return _worker_name; }

    nuraft::ptr< nuraft::rpc_client > create_client(const std::string& client) override;
    nuraft::ptr< nuraft::rpc_client > create_client(peer_id_t const& client);

    virtual nuraft::cmd_result_code create_client(peer_id_t const& client, nuraft::ptr< nuraft::rpc_client >&) = 0;

    virtual nuraft::cmd_result_code reinit_client(peer_id_t const& client, nuraft::ptr< nuraft::rpc_client >&) = 0;

    // Construct and send an AddServer message to the cluster
    NullAsyncResult add_server(uint32_t const srv_id, peer_id_t const& srv_addr, nuraft::srv_config const& dest_cfg);

    // Send a client request to the cluster
    NullAsyncResult append_entry(std::shared_ptr< nuraft::buffer > buf, nuraft::srv_config const& dest_cfg);

    // Construct and send a RemoveServer message to the cluster
    NullAsyncResult rem_server(uint32_t const srv_id, nuraft::srv_config const& dest_cfg);
};

} // namespace nuraft_mesg
