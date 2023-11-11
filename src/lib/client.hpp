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
//   Implements cornerstone's rpc_client::send(...) routine to translate
// and execute the call over gRPC asynchrously.
//
#pragma once

#include <libnuraft/nuraft.hxx>
#include <sisl/grpc/rpc_client.hpp>
#include <sisl/logging/logging.h>
#include <sisl/settings/settings.hpp>

#include "lib/common_lib.hpp"
#include "generated/nuraft_mesg_config_generated.h"

SETTINGS_INIT(nuraftmesgcfg::NuraftMesgConfig, nuraft_mesg_config);

#define NURAFT_MESG_CONFIG(...) SETTINGS_VALUE(nuraft_mesg_config, __VA_ARGS__)

namespace nuraft_mesg {

class grpc_resp : public nuraft::resp_msg {
public:
    using nuraft::resp_msg::resp_msg;
    ~grpc_resp() override = default;

    std::string dest_addr;
};

class grpc_base_client : public nuraft::rpc_client, public sisl::GrpcAsyncClient {
    static std::atomic_uint64_t _client_counter;
    uint64_t _client_id;

public:
    grpc_base_client() : nuraft::rpc_client::rpc_client(), sisl::GrpcAsyncClient("", nullptr, "", "") {}
    grpc_base_client(std::string const& worker_name, std::string const& addr,
                     const std::shared_ptr< sisl::GrpcTokenClient > token_client, std::string const& target_domain = "",
                     std::string const& ssl_cert = "") :
            nuraft::rpc_client::rpc_client(),
            sisl::GrpcAsyncClient(addr, token_client, target_domain, ssl_cert),
            _client_id(_client_counter++),
            _worker_name(worker_name.data()) {
        init();
    }
    ~grpc_base_client() override = default;

    void send(std::shared_ptr< nuraft::req_msg >& req, nuraft::rpc_handler& complete, uint64_t timeout_ms = 0) override;

    bool is_abandoned() const override { return false; }
    uint64_t get_id() const override { return _client_id; }

    void init() {
        if (!_generic_stub) _generic_stub = sisl::GrpcAsyncClient::make_generic_stub(_worker_name);
    }

    NullAsyncResult data_service_request_unidirectional(std::string const& request_name,
                                                        io_blob_list_t const& cli_buf) {
        grpc::ByteBuffer cli_byte_buf;
        serialize_to_byte_buffer(cli_byte_buf, cli_buf);
        return _generic_stub
            ->call_unary(cli_byte_buf, request_name,
                         NURAFT_MESG_CONFIG(mesg_factory_config->data_request_deadline_secs))
            .deferValue([](auto&& response) -> NullResult {
                if (response.hasError()) {
                    LOGE("Failed to send data_service_request, error: {}", response.error().error_message());
                    return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
                }
                return folly::unit;
            });
    }

    AsyncResult< sisl::io_blob > data_service_request_bidirectional(std::string const& request_name,
                                                                    io_blob_list_t const& cli_buf) {
        grpc::ByteBuffer cli_byte_buf;
        serialize_to_byte_buffer(cli_byte_buf, cli_buf);
        return _generic_stub
            ->call_unary(cli_byte_buf, request_name,
                         NURAFT_MESG_CONFIG(mesg_factory_config->data_request_deadline_secs))
            .deferValue([](auto&& response) -> Result< sisl::io_blob > {
                if (response.hasError()) {
                    LOGE("Failed to send data_service_request, error: {}", response.error().error_message());
                    return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
                }
                sisl::io_blob svr_buf;
                deserialize_from_byte_buffer(response.value(), svr_buf);
                return svr_buf;
            });
    }

protected:
    std::unique_ptr< sisl::GrpcAsyncClient::GenericAsyncStub > _generic_stub;
    char const* _worker_name;
};

template < typename TSERVICE >
class grpc_client : public grpc_base_client {
public:
    grpc_client(std::string const& worker_name, std::string const& addr,
                const std::shared_ptr< sisl::GrpcTokenClient > token_client, std::string const& target_domain = "",
                std::string const& ssl_cert = "") :
            grpc_base_client(worker_name, addr, token_client, target_domain, ssl_cert), _addr(addr) {
        init();
    }

    grpc_client(std::string const& worker_name, std::string const& addr, std::string const& target_domain = "",
                std::string const& ssl_cert = "") :
            grpc_client(worker_name, addr, nullptr, target_domain, ssl_cert) {}

    ~grpc_client() override = default;

    void init() override {
        // Re-create channel only if current channel is busted.
        if (_stub && is_connection_ready()) {
            LOGD("Channel looks fine, re-using");
            return;
        }
        LOGD("Client init ({}) to {}", (!!_stub ? "Again" : "First"), _addr);
        sisl::GrpcAsyncClient::init();
        _stub = sisl::GrpcAsyncClient::make_stub< TSERVICE >(_worker_name);
    }

protected:
    std::string const _addr;
    typename ::sisl::GrpcAsyncClient::AsyncStub< TSERVICE >::UPtr _stub;
};

} // namespace nuraft_mesg
