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

#include <libnuraft/async.hxx>
#include <memory>
#include <mutex>
#include <string>

#include <boost/uuid/uuid_io.hpp>

#include "grpc_factory.hpp"
#include <sisl/logging/logging.h>
#include <sisl/metrics/metrics.hpp>
#include "nuraft_mesg.hpp"

namespace sisl {
struct io_blob;
}

namespace nuraft_mesg {

class group_factory : public grpc_factory {
    std::shared_ptr< sisl::GrpcTokenClient > m_token_client;
    static std::string m_ssl_cert;

public:
    group_factory(int const cli_thread_count, group_id_t const& name,
                  std::shared_ptr< sisl::GrpcTokenClient > const token_client, std::string const& ssl_cert = "");

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
    std::shared_ptr< sisl::MetricsGroupWrapper > _metrics;

public:
    mesg_factory(std::shared_ptr< group_factory > g_factory, group_id_t const& grp_id, group_type_t const& grp_type,
                 std::shared_ptr< sisl::MetricsGroupWrapper > metrics = nullptr) :
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

    AsyncResult< sisl::io_blob > data_service_request_bidirectional(std::optional< Result< peer_id_t > > const&,
                                                                    std::string const&, io_blob_list_t const&);
};

} // namespace nuraft_mesg
