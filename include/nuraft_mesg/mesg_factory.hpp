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
#pragma once

#include <memory>
#include <string>

#include <folly/SharedMutex.h>
#include <sisl/logging/logging.h>
#include <libnuraft/rpc_cli_factory.hxx>
#include <libnuraft/srv_config.hxx>

#include "common.hpp"

namespace sisl {
struct io_blob;
class GrpcTokenClient;
class MetricsGroup;
} // namespace sisl

namespace nuraft_mesg {

using client_factory_lock_type = folly::SharedMutex;
// Brief:
//   Implements cornerstone's rpc_client_factory providing sisl::GrpcAsyncClient
//   inherited rpc_client instances sharing a common worker pool.
class grpc_factory : public nuraft::rpc_client_factory, public std::enable_shared_from_this< grpc_factory > {
    std::string _worker_name;

protected:
    client_factory_lock_type _client_lock;
    std::map< peer_id_t, std::shared_ptr< nuraft::rpc_client > > _clients;

public:
    grpc_factory(int const cli_thread_count, std::string const& name);
    grpc_factory(int const raft_cli_thread_count, int const data_cli_thread_count, std::string const& name);
    ~grpc_factory() override = default;

    std::string const raftWorkerName() const;
    std::string const dataWorkerName() const;

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

class group_factory : public grpc_factory {
    std::shared_ptr< sisl::GrpcTokenClient > m_token_client;
    static std::string m_ssl_cert;
    int m_max_receive_message_size;
    int m_max_send_message_size;

public:
    group_factory(int const cli_thread_count, group_id_t const& name,
                  std::shared_ptr< sisl::GrpcTokenClient > const token_client, std::string const& ssl_cert = "");

    group_factory(int const raft_cli_thread_count, int const data_cli_thread_count, group_id_t const& name,
                  std::shared_ptr< sisl::GrpcTokenClient > const token_client, std::string const& ssl_cert = "",
                  int const max_receive_message_size = 0, int const max_send_message_size = 0);

    using grpc_factory::create_client;
    nuraft::cmd_result_code create_client(peer_id_t const& client, nuraft::ptr< nuraft::rpc_client >&) override;
    nuraft::cmd_result_code reinit_client(peer_id_t const& client,
                                          std::shared_ptr< nuraft::rpc_client >& raft_client) override;

    virtual std::string lookupEndpoint(peer_id_t const& client) = 0;
};

class mesg_factory final : public grpc_factory {
    std::shared_ptr< group_factory > _group_factory;
    group_id_t const _group_id;
    group_type_t const _group_type;
    std::shared_ptr< sisl::MetricsGroup > _metrics;

public:
    mesg_factory(std::shared_ptr< group_factory > g_factory, group_id_t const& grp_id, group_type_t const& grp_type,
                 std::shared_ptr< sisl::MetricsGroup > metrics = nullptr) :
            grpc_factory(0, to_string(grp_id)),
            _group_factory(g_factory),
            _group_id(grp_id),
            _group_type(grp_type),
            _metrics(metrics) {}

    group_id_t group_id() const { return _group_id; }

    nuraft::cmd_result_code create_client(peer_id_t const& client, nuraft::ptr< nuraft::rpc_client >& rpc_ptr) override;

    nuraft::cmd_result_code reinit_client(peer_id_t const& client,
                                          std::shared_ptr< nuraft::rpc_client >& raft_client) override;

    NullAsyncResult data_service_request_unidirectional(std::optional< Result< peer_id_t > > const& dest,
                                                        std::string const& request_name, io_blob_list_t const& cli_buf);

    AsyncResult< sisl::GenericClientResponse >
    data_service_request_bidirectional(std::optional< Result< peer_id_t > > const&, std::string const&,
                                       io_blob_list_t const&);
};

} // namespace nuraft_mesg
