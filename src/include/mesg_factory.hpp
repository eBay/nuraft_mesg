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

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "grpc_factory.hpp"
#include <sisl/logging/logging.h>
#include <sisl/metrics/metrics.hpp>
#include "messaging_if.hpp"

namespace sisl {
struct io_blob;
}

namespace nuraft_mesg {

using group_name_t = std::string;
using group_type_t = std::string;

class mesg_client;

class group_factory : public grpc_factory {
    std::shared_ptr< sisl::TrfClient > m_trf_client;
    static std::string m_ssl_cert;

public:
    group_factory(int const cli_thread_count, std::string const& name, std::shared_ptr< sisl::TrfClient > const trf_client,
                  std::string const& ssl_cert = "");

    using grpc_factory::create_client;

    std::error_condition create_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >&) override;

    std::error_condition reinit_client(std::string const& client, std::shared_ptr< nuraft::rpc_client >& raft_client) override;

    virtual std::string lookupEndpoint(std::string const& client) = 0;
};

class mesg_factory final : public grpc_factory {
    std::shared_ptr< group_factory > _group_factory;
    group_name_t const _group_name;
    group_type_t const _group_type;
    std::shared_ptr< sisl::MetricsGroupWrapper > _metrics;

public:
    mesg_factory(std::shared_ptr< group_factory > g_factory, group_name_t const& grp_id, group_type_t const& grp_type,
                 std::shared_ptr< sisl::MetricsGroupWrapper > metrics = nullptr) :
            grpc_factory(0, grp_id),
            _group_factory(g_factory),
            _group_name(grp_id),
            _group_type(grp_type),
            _metrics(metrics) {}

    group_name_t group_name() const { return _group_name; }

    std::error_condition create_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >& rpc_ptr) override;

    std::error_condition reinit_client(const std::string& client, std::shared_ptr< nuraft::rpc_client >& raft_client) override;

    std::error_condition data_service_request(std::string const& request_name, io_blob_list_t const& cli_buf,
                                              data_service_response_handler_t const& response_cb);
};

} // namespace nuraft_mesg
