///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   grpc_factory static functions that makes for easy client creation.
//

#include <grpc_helper/rpc_client.hpp>

#include "grpc_client.hpp"
#include "grpc_factory.hpp"

namespace nuraft_grpc {

template < typename Payload >
struct client_ctx {
    int32_t _cur_dest;
    std::string const _new_srv_addr;

    client_ctx(Payload payload, shared< grpc_factory > factory, int32_t dest, std::string const& new_srv_addr = "") :
            _cur_dest(dest), _new_srv_addr(new_srv_addr), _payload(payload), _cli_factory(factory) {}

    Payload payload() const { return _payload; }
    shared< grpc_factory > cli_factory() const { return _cli_factory; }
    std::future< nuraft::cmd_result_code > future() { return _promise.get_future(); }
    void set(nuraft::cmd_result_code const code) { return _promise.set_value(code); }

private:
    Payload const _payload;
    shared< grpc_factory > _cli_factory;
    std::promise< nuraft::cmd_result_code > _promise;
};

template < typename PayloadType >
shared< nuraft::req_msg > createMessage(PayloadType payload, std::string const& srv_addr = "");

template <>
shared< nuraft::req_msg > createMessage(uint32_t const srv_id, std::string const& srv_addr) {
    assert(!srv_addr.empty());
    auto srv_conf = nuraft::srv_config(srv_id, srv_addr);
    auto log = std::make_shared< nuraft::log_entry >(0, srv_conf.serialize(), nuraft::log_val_type::cluster_server);
    auto msg = std::make_shared< nuraft::req_msg >(0, nuraft::msg_type::add_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template <>
shared< nuraft::req_msg > createMessage(shared< nuraft::buffer > buf, std::string const&) {
    auto log = std::make_shared< nuraft::log_entry >(0, buf);
    auto msg = std::make_shared< nuraft::req_msg >(0, nuraft::msg_type::client_request, 0, 1, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template <>
shared< nuraft::req_msg > createMessage(int32_t const srv_id, std::string const&) {
    auto buf = nuraft::buffer::alloc(sizeof(srv_id));
    buf->put(srv_id);
    buf->pos(0);
    auto log = std::make_shared< nuraft::log_entry >(0, buf, nuraft::log_val_type::cluster_server);
    auto msg = std::make_shared< nuraft::req_msg >(0, nuraft::msg_type::remove_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template < typename ContextType >
void respHandler(shared< ContextType > ctx, shared< nuraft::resp_msg >& rsp, shared< nuraft::rpc_exception >& err) {
    auto factory = ctx->cli_factory();
    if (err || !rsp) {
        LOGERROR("{}", (err ? err->what() : "No response."));
        ctx->set((rsp ? rsp->get_result_code() : nuraft::cmd_result_code::SERVER_NOT_FOUND));
        return;
    } else if (rsp->get_accepted()) {
        LOGDEBUGMOD(nuraft, "Accepted response");
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
    LOGDEBUGMOD(nuraft, "Updating destination from {} to {}[{}]", ctx->_cur_dest, rsp->get_dst(), gresp->dest_addr);
    ctx->_cur_dest = rsp->get_dst();
    auto client = factory->create_client(gresp->dest_addr);

    // We'll try again by forwarding the message
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](shared< nuraft::resp_msg >& rsp, shared< nuraft::rpc_exception >& err) { respHandler(ctx, rsp, err); });

    LOGDEBUGMOD(nuraft, "Creating new message: {}", ctx->_new_srv_addr);
    auto msg = createMessage(ctx->payload(), ctx->_new_srv_addr);
    client->send(msg, handler);
}

grpc_factory::grpc_factory(int const cli_thread_count, std::string const& name) :
        rpc_client_factory(), _worker_name(name) {
    if (0 < cli_thread_count) {
        grpc_helper::GrpcAsyncClientWorker::create_worker(_worker_name.data(), cli_thread_count);
    }
}

class grpc_error_client : public grpc_base_client {
    void send(RaftMessage const& message, handle_resp complete) override {
        auto null_msg = RaftMessage();
        auto status = ::grpc::Status(::grpc::ABORTED, "Bad connection");
        complete(null_msg, status);
    }
};

nuraft::ptr< nuraft::rpc_client > grpc_factory::create_client(const std::string& client) {
    nuraft::ptr< nuraft::rpc_client > new_client;

    std::lock_guard< std::mutex > lk(_client_lock);
    auto [it, happened] = _clients.emplace(client, nullptr);
    if (_clients.end() != it) {
        if (!happened) {
            LOGDEBUGMOD(nuraft, "Re-creating client for {}", client);
            if (auto err = reinit_client(client, it->second); err) {
                LOGERROR("Failed to re-initialize client {}: {}", client, err.message());
                new_client = std::make_shared< grpc_error_client >();
            } else {
                new_client = it->second;
            }
        } else {
            LOGDEBUGMOD(nuraft, "Creating client for {}", client);
            if (auto err = create_client(client, it->second); err) {
                LOGERROR("Failed to create client for {}: {}", client, err.message());
                new_client = std::make_shared< grpc_error_client >();
            } else {
                new_client = it->second;
            }
        }
        if (!it->second) { _clients.erase(it); }
    }
    return new_client;
}

std::future< nuraft::cmd_result_code > grpc_factory::add_server(uint32_t const srv_id, std::string const& srv_addr,
                                                                nuraft::srv_config const& dest_cfg) {
    auto client = create_client(dest_cfg.get_endpoint());
    assert(client);
    if (!client) {
        std::promise< nuraft::cmd_result_code > p;
        p.set_value(nuraft::CANCELLED);
        return p.get_future();
    }

    auto ctx = std::make_shared< client_ctx< uint32_t > >(srv_id, shared_from_this(), dest_cfg.get_id(), srv_addr);
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](shared< nuraft::resp_msg >& rsp, shared< nuraft::rpc_exception >& err) { respHandler(ctx, rsp, err); });

    auto msg = createMessage(srv_id, srv_addr);
    client->send(msg, handler);
    return ctx->future();
}

std::future< nuraft::cmd_result_code > grpc_factory::rem_server(uint32_t const srv_id,
                                                                nuraft::srv_config const& dest_cfg) {
    auto client = create_client(dest_cfg.get_endpoint());
    assert(client);
    if (!client) {
        std::promise< nuraft::cmd_result_code > p;
        p.set_value(nuraft::CANCELLED);
        return p.get_future();
    }

    auto ctx = std::make_shared< client_ctx< int32_t > >(srv_id, shared_from_this(), dest_cfg.get_id());
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](shared< nuraft::resp_msg >& rsp, shared< nuraft::rpc_exception >& err) { respHandler(ctx, rsp, err); });

    auto msg = createMessage(static_cast< int32_t >(srv_id));
    client->send(msg, handler);
    return ctx->future();
}

std::future< nuraft::cmd_result_code > grpc_factory::client_request(shared< nuraft::buffer > buf,
                                                                    nuraft::srv_config const& dest_cfg) {
    auto client = create_client(dest_cfg.get_endpoint());
    assert(client);
    if (!client) {
        std::promise< nuraft::cmd_result_code > p;
        p.set_value(nuraft::CANCELLED);
        return p.get_future();
    }

    auto ctx = std::make_shared< client_ctx< shared< nuraft::buffer > > >(buf, shared_from_this(), dest_cfg.get_id());
    auto handler = static_cast< nuraft::rpc_handler >(
        [ctx](shared< nuraft::resp_msg >& rsp, shared< nuraft::rpc_exception >& err) { respHandler(ctx, rsp, err); });

    auto msg = createMessage(buf);
    client->send(msg, handler);
    return ctx->future();
}

} // namespace nuraft_grpc
