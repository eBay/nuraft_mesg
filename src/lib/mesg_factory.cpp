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
#include "grpc_client.hpp"
#include <string>

#include "mesg_factory.hpp"
#include "service.hpp"
#include "proto/messaging_service.grpc.pb.h"
#include "utils.hpp"
#include "nuraft_mesg_config.hpp"

SISL_LOGGING_DECL(nuraft_mesg)

namespace nuraft_mesg {

std::string group_factory::m_ssl_cert;

class messaging_client : public grpc_client< Messaging >, public std::enable_shared_from_this< messaging_client > {
public:
    messaging_client(std::string const& worker_name, std::string const& addr,
                     const std::shared_ptr< sisl::TrfClient > trf_client, std::string const& target_domain = "",
                     std::string const& ssl_cert = "") :
            nuraft_mesg::grpc_client< Messaging >::grpc_client(worker_name, addr, trf_client, target_domain, ssl_cert) {
        _generic_stub = sisl::GrpcAsyncClient::make_generic_stub(_worker_name);
    }
    ~messaging_client() override = default;

    using grpc_base_client::send;

    std::atomic_uint bad_service;

    void send(RaftGroupMsg const& message, handle_resp complete) {
        auto weak_this = std::weak_ptr< messaging_client >(shared_from_this());
        auto group_compl = [weak_this, complete](auto response, auto status) mutable {
            if (::grpc::INVALID_ARGUMENT == status.error_code()) {
                if (auto mc = weak_this.lock(); mc) {
                    mc->bad_service.fetch_add(1, std::memory_order_relaxed);
                    LOGERRORMOD(
                        nuraft_mesg,
                        "Sent message to wrong service, need to disconnect! Error Message: [{}] Client IP: [{}]",
                        status.error_message(), mc->_addr);
                } else {
                    LOGERRORMOD(nuraft_mesg, "Sent message to wrong service, need to disconnect! Error Message: [{}]",
                                status.error_message());
                }
            }
            complete(*response.mutable_msg(), status);
        };

        _stub->call_unary< RaftGroupMsg, RaftGroupMsg >(
            message, &Messaging::StubInterface::AsyncRaftStep, group_compl,
            NURAFT_MESG_CONFIG(mesg_factory_config->raft_request_deadline_secs));
    }

    void data_service_request(std::string const& request_name, io_blob_list_t const& cli_buf,
                              data_service_response_handler_t const& response_cb) {
        grpc::ByteBuffer cli_byte_buf;
        serialize_to_byte_buffer(cli_byte_buf, cli_buf);
        _generic_stub->call_unary(
            cli_byte_buf, request_name,
            [response_cb](grpc::ByteBuffer& resp, ::grpc::Status& status) {
                if (!status.ok()) {
                    LOGERRORMOD(nuraft_mesg, "Failed to send data_service_request, error: {}", status.error_message());
                    return;
                }
                if (response_cb) {
                    sisl::io_blob svr_buf;
                    deserialize_from_byte_buffer(resp, svr_buf);
                    response_cb(svr_buf);
                }
            },
            NURAFT_MESG_CONFIG(mesg_factory_config->data_request_deadline_secs));
    }

protected:
    void send(RaftMessage const&, handle_resp) override { throw std::runtime_error("Bad call!"); }
    std::unique_ptr< sisl::GrpcAsyncClient::GenericAsyncStub > _generic_stub;
};

class group_client : public grpc_base_client {
    std::shared_ptr< messaging_client > _client;
    group_name_t const _group_name;
    group_type_t const _group_type;
    std::shared_ptr< group_metrics > _metrics;
    std::string const _client_addr;

public:
    group_client(std::shared_ptr< messaging_client > client, std::string const& client_addr,
                 group_name_t const& grp_name, group_type_t const& grp_type,
                 std::shared_ptr< sisl::MetricsGroupWrapper > metrics) :
            grpc_base_client(),
            _client(client),
            _group_name(grp_name),
            _group_type(grp_type),
            _metrics(std::static_pointer_cast< group_metrics >(metrics)),
            _client_addr(client_addr) {}

    ~group_client() override = default;

    std::shared_ptr< messaging_client > realClient() { return _client; }
    void setClient(std::shared_ptr< messaging_client > new_client) { _client = new_client; }

    void send(RaftMessage const& message, handle_resp complete) override {
        RaftGroupMsg group_msg;

        LOGTRACEMOD(nuraft_mesg, "Sending [{}] from: [{}] to: [{}] Group: [{}]",
                    nuraft::msg_type_to_string(nuraft::msg_type(message.base().type())), message.base().src(),
                    message.base().dest(), _group_name);
        if (_metrics) { COUNTER_INCREMENT(*_metrics, group_sends, 1); }
        group_msg.set_intended_addr(_client_addr);
        group_msg.set_group_name(_group_name);
        group_msg.set_group_type(_group_type);
        group_msg.mutable_msg()->CopyFrom(message);
        _client->send(group_msg, complete);
    }

    void data_service_request(std::string const& request_name, io_blob_list_t const& cli_buf,
                              data_service_response_handler_t const& response_cb) {
        _client->data_service_request(request_name, cli_buf, response_cb);
    }
};

std::error_condition mesg_factory::create_client(const std::string& client,
                                                 nuraft::ptr< nuraft::rpc_client >& raft_client) {
    // Re-direct this call to a global factory so we can re-use clients to the same endpoints
    LOGDEBUGMOD(nuraft_mesg, "Creating client to {}", client);
    auto m_client = std::dynamic_pointer_cast< messaging_client >(_group_factory->create_client(client));
    if (!m_client) { return std::make_error_condition(std::errc::connection_aborted); }
    raft_client = std::make_shared< group_client >(m_client, client, _group_name, _group_type, _metrics);
    return (!raft_client) ? std::make_error_condition(std::errc::invalid_argument) : std::error_condition();
}

std::error_condition mesg_factory::reinit_client(const std::string& client,
                                                 std::shared_ptr< nuraft::rpc_client >& raft_client) {
    LOGDEBUGMOD(nuraft_mesg, "Re-init client to {}", client);
    auto g_client = std::dynamic_pointer_cast< group_client >(raft_client);
    auto new_raft_client = std::static_pointer_cast< nuraft::rpc_client >(g_client->realClient());
    if (auto err = _group_factory->reinit_client(client, new_raft_client); err) { return err; }
    g_client->setClient(std::dynamic_pointer_cast< messaging_client >(new_raft_client));
    return std::error_condition();
}

std::error_condition mesg_factory::data_service_request(std::string const& request_name, io_blob_list_t const& cli_buf,
                                                        data_service_response_handler_t const& response_cb) {
    std::shared_lock< client_factory_lock_type > rl(_client_lock);
    for (auto& nuraft_client : _clients) {
        auto g_client = std::dynamic_pointer_cast< nuraft_mesg::group_client >(nuraft_client.second);
        g_client->data_service_request(get_generic_method_name(request_name, _group_name), cli_buf, response_cb);
    }
    return std::error_condition();
}

group_factory::group_factory(int const cli_thread_count, std::string const& name,
                             std::shared_ptr< sisl::TrfClient > const trf_client, std::string const& ssl_cert) :
        grpc_factory(cli_thread_count, name), m_trf_client(trf_client) {
    m_ssl_cert = ssl_cert;
}

std::error_condition group_factory::create_client(const std::string& client,
                                                  nuraft::ptr< nuraft::rpc_client >& raft_client) {
    LOGDEBUGMOD(nuraft_mesg, "Creating client to {}", client);
    auto endpoint = lookupEndpoint(client);
    if (endpoint.empty()) { return std::make_error_condition(std::errc::invalid_argument); }

    LOGDEBUGMOD(nuraft_mesg, "Creating client for [{}] @ [{}]", client, endpoint);
    raft_client = sisl::GrpcAsyncClient::make< messaging_client >(workerName(), endpoint, m_trf_client, "", m_ssl_cert);
    return (!raft_client) ? std::make_error_condition(std::errc::connection_aborted) : std::error_condition();
}

std::error_condition group_factory::reinit_client(const std::string& client,
                                                  std::shared_ptr< nuraft::rpc_client >& raft_client) {
    LOGDEBUGMOD(nuraft_mesg, "Re-init client to {}", client);
    assert(raft_client);
    auto mesg_client = std::dynamic_pointer_cast< messaging_client >(raft_client);
    if (!mesg_client->is_connection_ready() || 0 < mesg_client->bad_service.load(std::memory_order_relaxed)) {
        return create_client(client, raft_client);
    }
    return std::error_condition();
}

} // namespace nuraft_mesg
