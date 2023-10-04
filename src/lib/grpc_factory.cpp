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
//   grpc_factory static functions that makes for easy client creation.
//
#include <boost/uuid/string_generator.hpp>
#include <folly/Expected.h>
#include <folly/futures/Future.h>
#include <libnuraft/async.hxx>
#include <sisl/grpc/rpc_client.hpp>

#include "grpc_client.hpp"
#include "nuraft_mesg/grpc_factory.hpp"
#include "proto/raft_types.pb.h"

namespace nuraft_mesg {

template < typename Payload >
struct client_ctx {
    int32_t _cur_dest;
    peer_id_t const _new_srv_addr;

    client_ctx(Payload payload, std::shared_ptr< grpc_factory > factory, int32_t dest,
               peer_id_t const& new_srv_addr = peer_id_t()) :
            _cur_dest(dest), _new_srv_addr(new_srv_addr), _payload(payload), _cli_factory(factory) {}

    Payload payload() const { return _payload; }
    std::shared_ptr< grpc_factory > cli_factory() const { return _cli_factory; }
    NullAsyncResult future() {
        auto [p, sf] = folly::makePromiseContract< NullResult >();
        _promise = std::move(p);
        return sf;
    }
    void set(nuraft::cmd_result_code const code) {
        if (nuraft::OK == code)
            _promise.setValue(folly::Unit());
        else
            _promise.setValue(folly::makeUnexpected(code));
    }

private:
    Payload const _payload;
    std::shared_ptr< grpc_factory > _cli_factory;
    folly::Promise< NullResult > _promise;
};

template < typename PayloadType >
std::shared_ptr< nuraft::req_msg > createMessage(PayloadType payload, peer_id_t const& srv_addr = peer_id_t());

template <>
std::shared_ptr< nuraft::req_msg > createMessage(uint32_t const srv_id, peer_id_t const& srv_addr) {
    auto srv_conf = nuraft::srv_config(srv_id, to_string(srv_addr));
    auto log = std::make_shared< nuraft::log_entry >(0, srv_conf.serialize(), nuraft::log_val_type::cluster_server);
    auto msg = std::make_shared< nuraft::req_msg >(0, nuraft::msg_type::add_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template <>
std::shared_ptr< nuraft::req_msg > createMessage(std::shared_ptr< nuraft::buffer > buf, peer_id_t const&) {
    auto log = std::make_shared< nuraft::log_entry >(0, buf);
    auto msg = std::make_shared< nuraft::req_msg >(0, nuraft::msg_type::client_request, 0, 1, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template <>
std::shared_ptr< nuraft::req_msg > createMessage(int32_t const srv_id, peer_id_t const&) {
    auto buf = nuraft::buffer::alloc(sizeof(srv_id));
    buf->put(srv_id);
    buf->pos(0);
    auto log = std::make_shared< nuraft::log_entry >(0, buf, nuraft::log_val_type::cluster_server);
    auto msg = std::make_shared< nuraft::req_msg >(0, nuraft::msg_type::remove_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template < typename ContextType >
void respHandler(std::shared_ptr< ContextType > ctx, std::shared_ptr< nuraft::resp_msg >& rsp,
                 std::shared_ptr< nuraft::rpc_exception >& err) {
    auto factory = ctx->cli_factory();
    if (err || !rsp) {
        LOGERROR("{}", (err ? err->what() : "No response."));
        ctx->set((rsp ? rsp->get_result_code() : nuraft::cmd_result_code::SERVER_NOT_FOUND));
        return;
    } else if (rsp->get_accepted()) {
        LOGDEBUGMOD(nuraft_mesg, "Accepted response");
        ctx->set(rsp->get_result_code());
        return;
    } else if (ctx->_cur_dest == rsp->get_dst()) {
        LOGWARN("Request ignored");
        ctx->set(rsp->get_result_code());
        return;
    } else if (0 > rsp->get_dst()) {
        LOGWARN("No known leader!");
        ctx->set(rsp->get_result_code());
        return;
    }

    // Not accepted: means that `get_dst()` is a new leader.
    auto gresp = std::dynamic_pointer_cast< grpc_resp >(rsp);
    LOGDEBUGMOD(nuraft_mesg, "Updating destination from {} to {}[{}]", ctx->_cur_dest, rsp->get_dst(),
                gresp->dest_addr);
    ctx->_cur_dest = rsp->get_dst();
    auto client = factory->create_client(gresp->dest_addr);

    // We'll try again by forwarding the message
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](std::shared_ptr< nuraft::resp_msg >& rsp, std::shared_ptr< nuraft::rpc_exception >& err) {
            respHandler(ctx, rsp, err);
        });

    LOGDEBUGMOD(nuraft_mesg, "Creating new message: {}", ctx->_new_srv_addr);
    auto msg = createMessage(ctx->payload(), ctx->_new_srv_addr);
    client->send(msg, handler);
}

grpc_factory::grpc_factory(int const cli_thread_count, std::string const& name) :
        rpc_client_factory(), _worker_name(name) {
    if (0 < cli_thread_count) { sisl::GrpcAsyncClientWorker::create_worker(_worker_name.data(), cli_thread_count); }
}

class grpc_error_client : public grpc_base_client {
    void send(RaftMessage const& message, handle_resp complete) override {
        auto null_msg = RaftMessage();
        auto status = ::grpc::Status(::grpc::ABORTED, "Bad connection");
        complete(null_msg, status);
    }
};

nuraft::ptr< nuraft::rpc_client > grpc_factory::create_client(std::string const& client) {
    try {
        return create_client(boost::uuids::string_generator()(client));
    } catch (std::runtime_error const& e) { LOGCRITICAL("Client Endpoint Invalid! [{}]", client); }
    return nullptr;
}

nuraft::ptr< nuraft::rpc_client > grpc_factory::create_client(peer_id_t const& client) {
    nuraft::ptr< nuraft::rpc_client > new_client;

    std::unique_lock< client_factory_lock_type > lk(_client_lock);
    auto [it, happened] = _clients.emplace(client, nullptr);
    if (_clients.end() != it) {
        if (!happened) {
            LOGDEBUGMOD(nuraft_mesg, "Re-creating client for {}", client);
            if (auto err = reinit_client(client, it->second); nuraft::OK != err) {
                LOGERROR("Failed to re-initialize client {}: {}", client, err);
                new_client = std::make_shared< grpc_error_client >();
            } else {
                new_client = it->second;
            }
        } else {
            LOGDEBUGMOD(nuraft_mesg, "Creating client for {}", client);
            if (auto err = create_client(client, it->second); nuraft::OK != err) {
                LOGERROR("Failed to create client for {}: {}", client, err);
                new_client = std::make_shared< grpc_error_client >();
            } else {
                new_client = it->second;
            }
        }
        if (!it->second) { _clients.erase(it); }
    }
    return new_client;
}

NullAsyncResult grpc_factory::add_server(uint32_t const srv_id, peer_id_t const& srv_addr,
                                         nuraft::srv_config const& dest_cfg) {
    auto client = create_client(dest_cfg.get_endpoint());
    if (!client) { return folly::makeUnexpected(nuraft::CANCELLED); }

    auto ctx = std::make_shared< client_ctx< uint32_t > >(srv_id, shared_from_this(), dest_cfg.get_id(), srv_addr);
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](std::shared_ptr< nuraft::resp_msg >& rsp, std::shared_ptr< nuraft::rpc_exception >& err) {
            respHandler(ctx, rsp, err);
        });

    auto msg = createMessage(srv_id, srv_addr);
    client->send(msg, handler);
    return ctx->future();
}

NullAsyncResult grpc_factory::rem_server(uint32_t const srv_id, nuraft::srv_config const& dest_cfg) {
    auto client = create_client(dest_cfg.get_endpoint());
    if (!client) { return folly::makeUnexpected(nuraft::CANCELLED); }

    auto ctx = std::make_shared< client_ctx< int32_t > >(srv_id, shared_from_this(), dest_cfg.get_id());
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](std::shared_ptr< nuraft::resp_msg >& rsp, std::shared_ptr< nuraft::rpc_exception >& err) {
            respHandler(ctx, rsp, err);
        });

    auto msg = createMessage(static_cast< int32_t >(srv_id));
    client->send(msg, handler);
    return ctx->future();
}

NullAsyncResult grpc_factory::append_entry(std::shared_ptr< nuraft::buffer > buf, nuraft::srv_config const& dest_cfg) {
    auto client = create_client(dest_cfg.get_endpoint());
    if (!client) { return folly::makeUnexpected(nuraft::CANCELLED); }

    auto ctx =
        std::make_shared< client_ctx< std::shared_ptr< nuraft::buffer > > >(buf, shared_from_this(), dest_cfg.get_id());
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](std::shared_ptr< nuraft::resp_msg >& rsp, std::shared_ptr< nuraft::rpc_exception >& err) {
            respHandler(ctx, rsp, err);
        });

    auto msg = createMessage(buf);
    client->send(msg, handler);
    return ctx->future();
}

} // namespace nuraft_mesg
