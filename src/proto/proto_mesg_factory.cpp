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
#include <libnuraft/async.hxx>
#include <string>

#include <folly/futures/Future.h>

#include "nuraft_mesg/mesg_factory.hpp"
#include "lib/client.hpp"
#include "lib/service.hpp"
#include "lib/nuraft_mesg_config.hpp"

#include "messaging_service.grpc.pb.h"
#include "utils.hpp"

namespace nuraft_mesg {

std::string group_factory::m_ssl_cert;
using handle_resp = std::function< void(RaftMessage&, ::grpc::Status&) >;

static nuraft::cmd_result_code grpc_status_to_nuraft_code(::grpc::Status const& s) {
    if (s.ok()) { return nuraft::cmd_result_code::OK; }
    auto const ec = s.error_code();
    switch (ec) {
    case ::grpc::StatusCode::DEADLINE_EXCEEDED:
        return nuraft::cmd_result_code::TIMEOUT;
    case ::grpc::StatusCode::UNAVAILABLE:
    case ::grpc::StatusCode::NOT_FOUND:
        return nuraft::cmd_result_code::SERVER_NOT_FOUND;
    case ::grpc::StatusCode::CANCELLED:
    case ::grpc::StatusCode::ABORTED:
        return nuraft::cmd_result_code::CANCELLED;
    case ::grpc::StatusCode::FAILED_PRECONDITION:
        return nuraft::cmd_result_code::TERM_MISMATCH;
    case ::grpc::StatusCode::ALREADY_EXISTS:
        return nuraft::cmd_result_code::SERVER_ALREADY_EXISTS;
    case ::grpc::StatusCode::INVALID_ARGUMENT:
    case ::grpc::StatusCode::UNIMPLEMENTED:
    case ::grpc::StatusCode::UNAUTHENTICATED:
    case ::grpc::StatusCode::PERMISSION_DENIED:
    case ::grpc::StatusCode::RESOURCE_EXHAUSTED:
    case ::grpc::StatusCode::OUT_OF_RANGE:
        return nuraft::cmd_result_code::BAD_REQUEST;
    case ::grpc::StatusCode::DATA_LOSS:
    default:
        return nuraft::cmd_result_code::FAILED;
    }
}

static constexpr bool is_powerof2(uint64_t v) { return v && ((v & (v - 1)) == 0); }

static void log_every_nth(std::string const& addr, ::grpc::Status const& status, std::string const& msg_type) {
    static thread_local std::unordered_map< std::string, std::pair< uint64_t, Clock::time_point > > t_errors;
    static constexpr uint64_t every_nth_sec = 60;
    std::string msg = addr + "-" + status.error_message();

    uint64_t failed_count{1ul};
    if (auto const it = t_errors.find(msg); it != t_errors.end()) {
        if (get_elapsed_time_sec(it->second.second) > every_nth_sec) {
            it->second = std::pair(1ul, Clock::now()); // Reset
        } else {
            failed_count = ++(it->second.first);
        }
    } else {
        t_errors[msg] = std::pair(1ul, Clock::now());
    }

    if (is_powerof2(failed_count)) {
        LOGE("Failed {} time(s) in the last {} seconds to send {} data_service_request to {}, error: {}", failed_count,
             every_nth_sec, msg_type, addr, status.error_message());
    }
}

class messaging_client : public grpc_client< Messaging >, public std::enable_shared_from_this< messaging_client > {
public:
    messaging_client(std::string const& worker_name, std::string const& addr,
                     const std::shared_ptr< sisl::GrpcTokenClient > token_client, std::string const& target_domain = "",
                     std::string const& ssl_cert = "") :
            messaging_client(worker_name, worker_name, addr, token_client, target_domain, ssl_cert) {}

    messaging_client(std::string const& raft_worker_name, std::string const& data_worker_name, std::string const& addr,
                     const std::shared_ptr< sisl::GrpcTokenClient > token_client, std::string const& target_domain = "",
                     std::string const& ssl_cert = "") :
            nuraft_mesg::grpc_client< Messaging >::grpc_client(raft_worker_name, addr, token_client, target_domain,
                                                               ssl_cert) {
        _generic_stub = sisl::GrpcAsyncClient::make_generic_stub(data_worker_name);
    }
    ~messaging_client() override = default;

    using grpc_base_client::send;

    std::atomic_uint bad_service{0};

    void send(RaftGroupMsg const& message, handle_resp complete) {
        auto weak_this = std::weak_ptr< messaging_client >(shared_from_this());
        auto group_compl = [weak_this, complete](auto response, auto status) mutable {
            if (::grpc::INVALID_ARGUMENT == status.error_code()) {
                if (auto mc = weak_this.lock(); mc) {
                    mc->bad_service.fetch_add(1, std::memory_order_relaxed);
                    LOGE("Sent message to wrong service, need to disconnect! Error Message: [{}] Client IP: [{}]",
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
        return _generic_stub
            ->call_unary(cli_buf, request_name, NURAFT_MESG_CONFIG(mesg_factory_config->data_request_deadline_secs))
            .deferValue(
                [weak_this = std::weak_ptr< messaging_client >(shared_from_this())](auto&& response) -> NullResult {
                    if (response.hasError()) {
                        auto mc = weak_this.lock();
                        LOGD("Failed to send unidirectional data_service_request to {}, error: {}",
                             mc ? mc->_addr : "unknown", response.error().error_message());
                        return folly::makeUnexpected(grpc_status_to_nuraft_code(response.error()));
                    }
                    return folly::unit;
                });
    }

    AsyncResult< sisl::GenericClientResponse > data_service_request_bidirectional(std::string const& request_name,
                                                                                  io_blob_list_t const& cli_buf) {
        return _generic_stub
            ->call_unary(cli_buf, request_name, NURAFT_MESG_CONFIG(mesg_factory_config->data_request_deadline_secs))
            .deferValue([weak_this = std::weak_ptr< messaging_client >(shared_from_this())](
                            auto&& response) -> Result< sisl::GenericClientResponse > {
                if (response.hasError()) {
                    std::string addr = "unknown";
                    if (auto mc = weak_this.lock(); mc) {
                        mc->bad_service.fetch_add(1, std::memory_order_relaxed);
                        addr = mc->_addr;
                    }
                    log_every_nth(addr, response.error(), "bidirectional");
                    return folly::makeUnexpected(grpc_status_to_nuraft_code(response.error()));
                }
                return std::move(response.value());
            });
    }

protected:
    std::unique_ptr< sisl::GrpcAsyncClient::GenericAsyncStub > _generic_stub;
};

class grpc_proto_client : public grpc_base_client {
    std::shared_ptr< messaging_client > _client;
    group_id_t const _group_id;
    group_type_t const _group_type;
    std::shared_ptr< group_metrics > _metrics;
    std::string const _client_addr;

public:
    grpc_proto_client(std::shared_ptr< messaging_client > client, peer_id_t const& client_addr,
                      group_id_t const& grp_name, group_type_t const& grp_type,
                      std::shared_ptr< sisl::MetricsGroupWrapper > metrics) :
            grpc_base_client(),
            _client(client),
            _group_id(grp_name),
            _group_type(grp_type),
            _metrics(std::static_pointer_cast< group_metrics >(metrics)),
            _client_addr(to_string(client_addr)) {}

    ~grpc_proto_client() override = default;

    std::shared_ptr< messaging_client > realClient() { return _client; }
    void setClient(std::shared_ptr< messaging_client > new_client) { _client = new_client; }
    bool reinitRequired() const { return (!_client || 0 < _client->bad_service.load(std::memory_order_relaxed)); }

    void send(RaftMessage const& message, handle_resp complete) {
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

    AsyncResult< sisl::GenericClientResponse > data_service_request_bidirectional(std::string const& request_name,
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
    raft_client = std::make_shared< grpc_proto_client >(m_client, client, _group_id, _group_type, _metrics);
    return (!raft_client) ? nuraft::BAD_REQUEST : nuraft::OK;
}

nuraft::cmd_result_code mesg_factory::reinit_client(peer_id_t const& client,
                                                    std::shared_ptr< nuraft::rpc_client >& raft_client) {
    LOGD("Re-init client to {}", client);
    auto g_client = std::dynamic_pointer_cast< grpc_proto_client >(raft_client);
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
            auto g_client = std::dynamic_pointer_cast< nuraft_mesg::grpc_proto_client >(it->second);
            return g_client->data_service_request_unidirectional(get_generic_method_name(request_name, _group_id),
                                                                 cli_buf);
        } else {
            LOGE("Failed to find client for [{}], request name [{}]", dest->value(), request_name);
            return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
        }
    }
    // else
    for (auto& nuraft_client : _clients) {
        auto g_client = std::dynamic_pointer_cast< nuraft_mesg::grpc_proto_client >(nuraft_client.second);
        calls.push_back(
            g_client->data_service_request_unidirectional(get_generic_method_name(request_name, _group_id), cli_buf));
    }
    // We ignore the vector of future response from collect all and st the value as folly::unit.
    // This is because we do not have a use case to handle the errors that happen during the unidirectional call to
    // all the peers.
    return folly::collectAll(calls).deferValue([](auto&&) -> NullResult { return folly::unit; });
}

AsyncResult< sisl::GenericClientResponse >
mesg_factory::data_service_request_bidirectional(std::optional< Result< peer_id_t > > const& dest,
                                                 std::string const& request_name, io_blob_list_t const& cli_buf) {
    if (!dest) {
        LOGE("Cannot send request to all the peers, not implemented yet!. Request name [{}]", request_name);
        return folly::makeUnexpected(nuraft::cmd_result_code::BAD_REQUEST);
    }
    if (dest->hasError()) return folly::makeUnexpected(dest->error());

    {
        std::shared_lock< client_factory_lock_type > rl(_client_lock);
        if (auto it = _clients.find(dest->value()); _clients.end() != it) {
            if (auto g_client = std::dynamic_pointer_cast< nuraft_mesg::grpc_proto_client >(it->second);
                !g_client->reinitRequired()) {
                return g_client->data_service_request_bidirectional(get_generic_method_name(request_name, _group_id),
                                                                    cli_buf);
            }
        } else {
            LOGE("Failed to find client for [{}], request name [{}]", dest->value(), request_name);
            return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
        }
    }

    std::unique_lock< client_factory_lock_type > wl(_client_lock);
    auto it = _clients.find(dest->value());
    if (auto err = reinit_client(dest->value(), it->second); nuraft::OK != err) {
        LOGD("Failed to re-initialize client {}: {}", dest->value(), err);
        return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
    }
    auto g_client = std::dynamic_pointer_cast< nuraft_mesg::grpc_proto_client >(it->second);
    return g_client->data_service_request_bidirectional(get_generic_method_name(request_name, _group_id), cli_buf);
}

group_factory::group_factory(int const cli_thread_count, group_id_t const& name,
                             std::shared_ptr< sisl::GrpcTokenClient > const token_client, std::string const& ssl_cert) :
        group_factory(cli_thread_count, 0, name, token_client, ssl_cert) {}

group_factory::group_factory(int const raft_cli_thread_count, int const data_cli_thread_count, group_id_t const& name,
                             std::shared_ptr< sisl::GrpcTokenClient > const token_client, std::string const& ssl_cert) :
        grpc_factory(raft_cli_thread_count, data_cli_thread_count, to_string(name)), m_token_client(token_client) {
    m_ssl_cert = ssl_cert;
}

nuraft::cmd_result_code group_factory::create_client(peer_id_t const& client,
                                                     nuraft::ptr< nuraft::rpc_client >& raft_client) {
    LOGD("Creating client to {}", client);
    auto endpoint = lookupEndpoint(client);
    if (endpoint.empty()) return nuraft::BAD_REQUEST;

    LOGD("Creating client for [{}] @ [{}]", client, endpoint);
    raft_client = sisl::GrpcAsyncClient::make< messaging_client >(raftWorkerName(), dataWorkerName(), endpoint,
                                                                  m_token_client, "", m_ssl_cert);
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

inline LogEntry* fromLogEntry(nuraft::log_entry const& entry, LogEntry* log) {
    log->set_term(entry.get_term());
    log->set_type((LogType)entry.get_val_type());
    auto& buffer = entry.get_buf();
    buffer.pos(0);
    log->set_buffer(buffer.data(), buffer.size());
    log->set_timestamp(entry.get_timestamp());
    return log;
}

inline RCRequest* fromRCRequest(nuraft::req_msg& rcmsg) {
    auto req = new RCRequest;
    req->set_last_log_term(rcmsg.get_last_log_term());
    req->set_last_log_index(rcmsg.get_last_log_idx());
    req->set_commit_index(rcmsg.get_commit_idx());
    for (auto& rc_entry : rcmsg.log_entries()) {
        auto entry = req->add_log_entries();
        fromLogEntry(*rc_entry, entry);
    }
    return req;
}

inline std::shared_ptr< nuraft::resp_msg > toResponse(RaftMessage const& raft_msg) {
    if (!raft_msg.has_rc_response()) return nullptr;
    auto const& base = raft_msg.base();
    auto const& resp = raft_msg.rc_response();
    auto message = std::make_shared< grpc_resp >(base.term(), (nuraft::msg_type)base.type(), base.src(), base.dest(),
                                                 resp.next_index(), resp.accepted());
    message->set_next_batch_size_hint_in_bytes(resp.batch_size_hint());
    message->set_result_code((nuraft::cmd_result_code)(0 - resp.result_code()));
    if (nuraft::cmd_result_code::NOT_LEADER == message->get_result_code()) {
        LOGI("Leader has changed!");
        message->dest_addr = resp.dest_addr();
    }
    if (0 < resp.context().length()) {
        auto ctx_buffer = nuraft::buffer::alloc(resp.context().length());
        memcpy(ctx_buffer->data(), resp.context().data(), resp.context().length());
        message->set_ctx(ctx_buffer);
    }
    return message;
}

std::atomic_uint64_t grpc_base_client::_client_counter = 0ul;

///
// This is where the magic of serialization happens starting with creating a RaftMessage and invoking our
// specific ::send() which will later transform into a RaftGroupMsg
void grpc_base_client::send(std::shared_ptr< nuraft::req_msg >& req, nuraft::rpc_handler& complete, uint64_t) {
    assert(req && complete);
    RaftMessage grpc_request;
    grpc_request.set_allocated_base(fromBaseRequest(*req));
    grpc_request.set_allocated_rc_request(fromRCRequest(*req));

    LOGT("Sending [{}] from: [{}] to: [{}]", nuraft::msg_type_to_string(nuraft::msg_type(grpc_request.base().type())),
         grpc_request.base().src(), grpc_request.base().dest());

    static_cast< grpc_proto_client* >(this)->send(
        grpc_request, [req, complete](RaftMessage& response, ::grpc::Status& status) mutable -> void {
            std::shared_ptr< nuraft::rpc_exception > err;
            std::shared_ptr< nuraft::resp_msg > resp;

            if (status.ok()) {
                resp = toResponse(response);
                if (!resp) { err = std::make_shared< nuraft::rpc_exception >("missing response", req); }
            } else {
                err = std::make_shared< nuraft::rpc_exception >(status.error_message(), req);
            }
            complete(resp, err);
        });
}

} // namespace nuraft_mesg
