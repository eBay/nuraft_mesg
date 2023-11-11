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

#include "lib/common_lib.hpp"

namespace nuraft_mesg {

class grpc_resp : public nuraft::resp_msg {
public:
    using nuraft::resp_msg::resp_msg;
    ~grpc_resp() override = default;

    std::string dest_addr;
};

class grpc_base_client : public nuraft::rpc_client {
    static std::atomic_uint64_t _client_counter;
    uint64_t _client_id;

public:
    grpc_base_client() : nuraft::rpc_client::rpc_client(), _client_id(_client_counter++) {}
    ~grpc_base_client() override = default;

    void send(std::shared_ptr< nuraft::req_msg >& req, nuraft::rpc_handler& complete, uint64_t timeout_ms = 0) override;

    bool is_abandoned() const override { return false; }
    uint64_t get_id() const override { return _client_id; }
};

template < typename TSERVICE >
class grpc_client : public grpc_base_client, public sisl::GrpcAsyncClient {
public:
    grpc_client(std::string const& worker_name, std::string const& addr,
                const std::shared_ptr< sisl::GrpcTokenClient > token_client, std::string const& target_domain = "",
                std::string const& ssl_cert = "") :
            grpc_base_client(),
            sisl::GrpcAsyncClient(addr, token_client, target_domain, ssl_cert),
            _addr(addr),
            _worker_name(worker_name.data()) {
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
    char const* _worker_name;
    typename ::sisl::GrpcAsyncClient::AsyncStub< TSERVICE >::UPtr _stub;
};

} // namespace nuraft_mesg
