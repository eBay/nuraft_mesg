
#include <grpcpp/create_channel.h>

#include "grpc_factory.hpp"
#include "grpc_service.hpp"

namespace cornerstone {

template<typename Payload>
struct client_ctx {
   client_ctx(Payload payload, shared<grpc_factory> factory) :
      _payload(payload),
      _cli_factory(factory)
   { }

   Payload payload() const                      { return _payload; }
   shared<grpc_factory> cli_factory() const     { return _cli_factory; }
   std::future<bool> future()                   { return _promise.get_future(); }
   void set(bool const success)                 { return _promise.set_value(success); }

 private:
   Payload const           _payload;
   shared<grpc_factory>    _cli_factory;

   std::condition_variable _join_cv;
   std::mutex              _join_lk;

   std::promise<bool> _promise;
};

template<typename PayloadType>
ptr<req_msg>
createMessage(PayloadType payload);

template<>
ptr<req_msg>
createMessage(uint32_t const srv_id) {
   auto srv_addr = std::to_string(srv_id);
   auto srv_conf = srv_config(srv_id, srv_addr);
   auto log = std::make_shared<log_entry>(
      0,
      srv_conf.serialize(),
      log_val_type::cluster_server
      );
   auto msg = std::make_shared<req_msg>(0, msg_type::add_server_request, 0, 0, 0, 0, 0);
   msg->log_entries().push_back(log);
   return msg;
}

template<>
ptr<req_msg>
createMessage(shared<buffer> buf) {
   auto log = std::make_shared<log_entry>(0, buf);
   auto msg = std::make_shared<req_msg>(0, msg_type::client_request, 0, 1, 0, 0, 0);
   msg->log_entries().push_back(log);
   return msg;
}

template<>
ptr<req_msg>
createMessage(int32_t const srv_id) {
    auto buf = buffer::alloc(sizeof(srv_id));
    buf->put(srv_id);
    buf->pos(0);
    auto log = std::make_shared<log_entry>(0, buf, log_val_type::cluster_server);
    auto msg = std::make_shared<req_msg>(0, msg_type::remove_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template<typename ContextType>
void
respHandler(shared<ContextType> ctx,
            ptr<resp_msg>& rsp,
            ptr<rpc_exception>& err) {
   if (err) {
      LOGERRORMOD(raft_core, "{}", err->what());
      ctx->set(false);
      return;
   } else if (rsp->get_accepted()) {
      LOGDEBUGMOD(raft_core, "Successfully sent message.");
      ctx->set(true);
      return;
   } else if (0 > rsp->get_dst()) {
      LOGERRORMOD(raft_core, "No known leader!");
      ctx->set(false);
      return;
   }

   // Not accepted: means that `get_dst()` is a new leader.
   auto factory = ctx->cli_factory();
   LOGDEBUGMOD(raft_core, "Updating leader from {} to {}", factory->current_leader(), rsp->get_dst());
   factory->update_leader(rsp->get_dst());
   auto client = factory->create_client(std::to_string(rsp->get_dst()));

   // We'll try again by forwarding the message
   auto handler = static_cast<rpc_handler>([ctx] (ptr<resp_msg>& rsp,
                                 ptr<rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(ctx->payload());
   client->send(msg, handler);
}

std::future<bool>
grpc_factory::add_server(uint32_t const srv_id, shared<grpc_factory> factory) {
   auto client = factory->create_client(std::to_string(factory->current_leader()));
   assert(client);

   auto ctx = std::make_shared<client_ctx<uint32_t>>(srv_id, factory);
   auto handler = static_cast<rpc_handler>([ctx] (ptr<resp_msg>& rsp,
                                 ptr<rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(srv_id);
   client->send(msg, handler);
   return ctx->future();
}

std::future<bool>
grpc_factory::rem_server(uint32_t const srv_id, shared<grpc_factory> factory) {
   auto client = factory->create_client(std::to_string(factory->current_leader()));
   assert(client);

   auto ctx = std::make_shared<client_ctx<uint32_t>>(srv_id, factory);
   auto handler = static_cast<rpc_handler>([ctx] (ptr<resp_msg>& rsp,
                                 ptr<rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(srv_id);
   client->send(msg, handler);
   return ctx->future();
}

std::future<bool>
grpc_factory::client_request(shared<buffer> buf, shared<grpc_factory> factory) {
   auto client = factory->create_client(std::to_string(factory->current_leader()));
   assert(client);

   auto ctx = std::make_shared<client_ctx<shared<buffer>>>(buf, factory);
   auto handler = static_cast<rpc_handler>([ctx] (ptr<resp_msg>& rsp,
                                 ptr<rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(buf);
   client->send(msg, handler);
   return ctx->future();
}

void
grpc_client::send(ptr<req_msg>& req, rpc_handler& complete) {
   ptr<rpc_exception> err;
   ptr<resp_msg> resp;

   ::grpc::ClientContext context;
   raft_core::RaftMessage response;
   auto status = send(&context, fromRequest(*req), &response);

   if (status.ok()) {
      resp = toResponse(response);
   } else {
      err = std::make_shared<rpc_exception>(status.error_message(), req);
   }
   complete(resp, err);
}

::grpc::Status
simple_grpc_client::send(::grpc::ClientContext *ctx,
                    raft_core::RaftMessage const &message,
                    raft_core::RaftMessage *response) {
   LOGTRACEMOD(raft_core, "Sending [{}] from: [{}] to: [{}]",
         message.base().type(),
         message.base().src(),
         message.base().dest()
         );
   return _stub->Step(ctx, message, response);
}

}
