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
#include "proto_client.hpp"
#include <libnuraft/async.hxx>
#include <string>

#include <folly/futures/Future.h>
#include <sisl/settings/settings.hpp>

#include "nuraft_mesg/mesg_factory.hpp"
#include "lib/service.hpp"
#include "messaging_service.grpc.pb.h"
#include "lib/generated/nuraft_mesg_config_generated.h"

SETTINGS_INIT(nuraftmesgcfg::NuraftMesgConfig, nuraft_mesg_config);

#define NURAFT_MESG_CONFIG_WITH(...) SETTINGS(nuraft_mesg_config, __VA_ARGS__)
#define NURAFT_MESG_CONFIG_THIS(...) SETTINGS_THIS(nuraft_mesg_config, __VA_ARGS__)
#define NURAFT_MESG_CONFIG(...) SETTINGS_VALUE(nuraft_mesg_config, __VA_ARGS__)

#define NURAFT_MESG_SETTINGS_FACTORY() SETTINGS_FACTORY(nuraft_mesg_config)

namespace nuraft_mesg {

std::string group_factory::m_ssl_cert;

class messaging_client : public grpc_client< Messaging >, public std::enable_shared_from_this< messaging_client > {
public:
    messaging_client(std::string const& worker_name, std::string const& addr,
                     const std::shared_ptr< sisl::GrpcTokenClient > token_client, std::string const& target_domain = "",
                     std::string const& ssl_cert = "") :
            nuraft_mesg::grpc_client< Messaging >::grpc_client(worker_name, addr, token_client, target_domain,
                                                               ssl_cert) {
        _generic_stub = sisl::GrpcAsyncClient::make_generic_stub(_worker_name);
    }
    ~messaging_client() override = default;

    using grpc_base_client::send;

    std::atomic_uint bad_service;

    void send(RaftGroupMsg const& message, grpc_proto_client::handle_resp complete) {
        auto weak_this = std::weak_ptr< messaging_client >(shared_from_this());
        auto group_compl = [weak_this, complete](auto response, auto status) mutable {
            if (::grpc::INVALID_ARGUMENT == status.error_code()) {
                if (auto mc = weak_this.lock(); mc) {
                    mc->bad_service.fetch_add(1, std::memory_order_relaxed);
                    LOGE(

                        "Sent message to wrong service, need to disconnect! Error Message: [{}] Client IP: [{}]",
                        status.error_message(), mc->_addr);
                } else {
                    LOGE("Sent message to wrong service, need to disconnect! Error Message: [{}]",
                         status.error_message());
                }
            }
            complete(*response.mutable_msg(), status);
        };

        _stub->call_unary< RaftGroupMsg, RaftGroupMsg >(
            message, &Messaging::StubInterface::AsyncRaftStep, group_compl,
            NURAFT_MESG_CONFIG(mesg_factory_config->raft_request_deadline_secs));
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
};

class group_client : public grpc_proto_client {
    std::shared_ptr< messaging_client > _client;
    group_id_t const _group_id;
    group_type_t const _group_type;
    std::shared_ptr< group_metrics > _metrics;
    std::string const _client_addr;

public:
    group_client(std::shared_ptr< messaging_client > client, peer_id_t const& client_addr, group_id_t const& grp_name,
                 group_type_t const& grp_type, std::shared_ptr< sisl::MetricsGroupWrapper > metrics) :
            grpc_proto_client(),
            _client(client),
            _group_id(grp_name),
            _group_type(grp_type),
            _metrics(std::static_pointer_cast< group_metrics >(metrics)),
            _client_addr(to_string(client_addr)) {}

    ~group_client() override = default;

    std::shared_ptr< messaging_client > realClient() { return _client; }
    void setClient(std::shared_ptr< messaging_client > new_client) { _client = new_client; }

    void send(RaftMessage const& message, handle_resp complete) override {
        RaftGroupMsg group_msg;

        LOGT("Sending [{}] from: [{}] to: [{}] Group: [{}]",
             nuraft::msg_type_to_string(nuraft::msg_type(message.base().type())), message.base().src(),
             message.base().dest(), _group_id);
        if (_metrics) { COUNTER_INCREMENT(*_metrics, group_sends, 1); }
        group_msg.set_intended_addr(_client_addr);
        group_msg.set_group_id(to_string(_group_id));
        group_msg.set_group_type(_group_type);
        group_msg.mutable_msg()->CopyFrom(message);
        _client->send(group_msg, complete);
    }

    NullAsyncResult data_service_request_unidirectional(std::string const& request_name,
                                                        io_blob_list_t const& cli_buf) {
        return _client->data_service_request_unidirectional(request_name, cli_buf);
    }

    AsyncResult< sisl::io_blob > data_service_request_bidirectional(std::string const& request_name,
                                                                    io_blob_list_t const& cli_buf) {
        return _client->data_service_request_bidirectional(request_name, cli_buf);
    }
};

nuraft::cmd_result_code mesg_factory::create_client(peer_id_t const& client,
                                                    nuraft::ptr< nuraft::rpc_client >& raft_client) {
    // Re-direct this call to a global factory so we can re-use clients to the same endpoints
    LOGD("Creating client to {}", client);
    auto m_client = std::dynamic_pointer_cast< messaging_client >(_group_factory->create_client(to_string(client)));
    if (!m_client) return nuraft::CANCELLED;
    raft_client = std::make_shared< group_client >(m_client, client, _group_id, _group_type, _metrics);
    return (!raft_client) ? nuraft::BAD_REQUEST : nuraft::OK;
}

nuraft::cmd_result_code mesg_factory::reinit_client(peer_id_t const& client,
                                                    std::shared_ptr< nuraft::rpc_client >& raft_client) {
    LOGD("Re-init client to {}", client);
    auto g_client = std::dynamic_pointer_cast< group_client >(raft_client);
    auto new_raft_client = std::static_pointer_cast< nuraft::rpc_client >(g_client->realClient());
    if (auto err = _group_factory->reinit_client(client, new_raft_client); err) { return err; }
    g_client->setClient(std::dynamic_pointer_cast< messaging_client >(new_raft_client));
    return nuraft::OK;
}

NullAsyncResult mesg_factory::data_service_request_unidirectional(std::optional< Result< peer_id_t > > const& dest,
                                                                  std::string const& request_name,
                                                                  io_blob_list_t const& cli_buf) {
    std::shared_lock< client_factory_lock_type > rl(_client_lock);
    auto calls = std::vector< NullAsyncResult >();
    if (dest) {
        if (dest->hasError()) return folly::makeUnexpected(dest->error());
        if (auto it = _clients.find(dest->value()); _clients.end() != it) {
            auto g_client = std::dynamic_pointer_cast< nuraft_mesg::group_client >(it->second);
            return g_client->data_service_request_unidirectional(get_generic_method_name(request_name, _group_id),
                                                                 cli_buf);
        } else {
            LOGE("Failed to find client for [{}], request name [{}]", dest->value(), request_name);
            return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
        }
    }
    // else
    for (auto& nuraft_client : _clients) {
        auto g_client = std::dynamic_pointer_cast< nuraft_mesg::group_client >(nuraft_client.second);
        calls.push_back(
            g_client->data_service_request_unidirectional(get_generic_method_name(request_name, _group_id), cli_buf));
    }
    // We ignore the vector of future response from collect all and st the value as folly::unit.
    // This is because we do not have a use case to handle the errors that happen during the unidirectional call to all
    // the peers.
    return folly::collectAll(calls).deferValue([](auto&&) -> NullResult { return folly::unit; });
}

AsyncResult< sisl::io_blob >
mesg_factory::data_service_request_bidirectional(std::optional< Result< peer_id_t > > const& dest,
                                                 std::string const& request_name, io_blob_list_t const& cli_buf) {
    std::shared_lock< client_factory_lock_type > rl(_client_lock);
    auto calls = std::vector< AsyncResult< sisl::io_blob > >();
    if (dest) {
        if (dest->hasError()) return folly::makeUnexpected(dest->error());
        if (auto it = _clients.find(dest->value()); _clients.end() != it) {
            auto g_client = std::dynamic_pointer_cast< nuraft_mesg::group_client >(it->second);
            return g_client->data_service_request_bidirectional(get_generic_method_name(request_name, _group_id),
                                                                cli_buf);
        } else {
            LOGE("Failed to find client for [{}], request name [{}]", dest->value(), request_name);
            return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
        }
    }
    // else
    LOGE("Cannot send request to all the peers, not implemented yet!. Request name [{}]", request_name);
    return folly::makeUnexpected(nuraft::cmd_result_code::BAD_REQUEST);
}

group_factory::group_factory(int const cli_thread_count, group_id_t const& name,
                             std::shared_ptr< sisl::GrpcTokenClient > const token_client, std::string const& ssl_cert) :
        grpc_factory(cli_thread_count, to_string(name)), m_token_client(token_client) {
    m_ssl_cert = ssl_cert;
}

nuraft::cmd_result_code group_factory::create_client(peer_id_t const& client,
                                                     nuraft::ptr< nuraft::rpc_client >& raft_client) {
    LOGD("Creating client to {}", client);
    auto endpoint = lookupEndpoint(client);
    if (endpoint.empty()) return nuraft::BAD_REQUEST;

    LOGD("Creating client for [{}] @ [{}]", client, endpoint);
    raft_client =
        sisl::GrpcAsyncClient::make< messaging_client >(workerName(), endpoint, m_token_client, "", m_ssl_cert);
    return (!raft_client) ? nuraft::CANCELLED : nuraft::OK;
}

nuraft::cmd_result_code group_factory::reinit_client(peer_id_t const& client,
                                                     std::shared_ptr< nuraft::rpc_client >& raft_client) {
    LOGD("Re-init client to {}", client);
    assert(raft_client);
    auto mesg_client = std::dynamic_pointer_cast< messaging_client >(raft_client);
    if (!mesg_client->is_connection_ready() || 0 < mesg_client->bad_service.load(std::memory_order_relaxed)) {
        return create_client(client, raft_client);
    }
    return nuraft::OK;
}

} // namespace nuraft_mesg
