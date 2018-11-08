
#include "factory.h"
#include "messaging_service.grpc.pb.h"

namespace sds_messaging {

namespace sdsmsg = ::sds::messaging;

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
cstn::ptr<cstn::req_msg>
createMessage(PayloadType payload);

template<>
cstn::ptr<cstn::req_msg>
createMessage(uint32_t const srv_id) {
   auto srv_addr = std::to_string(srv_id);
   auto srv_conf = cstn::srv_config(srv_id, srv_addr);
   auto log = std::make_shared<cstn::log_entry>(
      0,
      srv_conf.serialize(),
      cstn::log_val_type::cluster_server
      );
   auto msg = std::make_shared<cstn::req_msg>(0, cstn::msg_type::add_server_request, 0, 0, 0, 0, 0);
   msg->log_entries().push_back(log);
   return msg;
}

template<>
cstn::ptr<cstn::req_msg>
createMessage(shared<cstn::buffer> buf) {
   auto log = std::make_shared<cstn::log_entry>(0, buf);
   auto msg = std::make_shared<cstn::req_msg>(0, cstn::msg_type::client_request, 0, 1, 0, 0, 0);
   msg->log_entries().push_back(log);
   return msg;
}

template<>
cstn::ptr<cstn::req_msg>
createMessage(int32_t const srv_id) {
    auto buf = cstn::buffer::alloc(sizeof(srv_id));
    buf->put(srv_id);
    buf->pos(0);
    auto log = std::make_shared<cstn::log_entry>(0, buf, cstn::log_val_type::cluster_server);
    auto msg = std::make_shared<cstn::req_msg>(0, cstn::msg_type::remove_server_request, 0, 0, 0, 0, 0);
    msg->log_entries().push_back(log);
    return msg;
}

template<typename ContextType>
void
respHandler(shared<ContextType> ctx,
            cstn::ptr<cstn::resp_msg>& rsp,
            cstn::ptr<cstn::rpc_exception>& err) {
   if (err) {
      LOGERRORMOD(sds_msg, "{}", err->what());
      ctx->set(false);
      return;
   } else if (rsp->get_accepted()) {
      LOGDEBUGMOD(sds_msg, "Successfully sent message.");
      ctx->set(true);
      return;
   } else if (0 > rsp->get_dst()) {
      LOGERRORMOD(sds_msg, "No known leader!");
      ctx->set(false);
      return;
   }

   // Not accepted: means that `get_dst()` is a new leader.
   auto factory = ctx->cli_factory();
   LOGDEBUGMOD(sds_msg, "Updating leader from {} to {}", factory->current_leader(), rsp->get_dst());
   factory->update_leader(rsp->get_dst());
   auto client = factory->create_client(std::to_string(rsp->get_dst()));

   // We'll try again by forwarding the message
   auto handler = static_cast<cstn::rpc_handler>([ctx] (cstn::ptr<cstn::resp_msg>& rsp,
                                 cstn::ptr<cstn::rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(ctx->payload());
   client->send(msg, handler);
}

std::future<bool>
grpc_factory::add_server(uint32_t const srv_id, shared<grpc_factory> factory) {
   auto client = factory->create_client();
   assert(client);

   auto ctx = std::make_shared<client_ctx<uint32_t>>(srv_id, factory);
   auto handler = static_cast<cstn::rpc_handler>([ctx] (cstn::ptr<cstn::resp_msg>& rsp,
                                 cstn::ptr<cstn::rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(srv_id);
   client->send(msg, handler);
   return ctx->future();
}

std::future<bool>
grpc_factory::rem_server(uint32_t const srv_id, shared<grpc_factory> factory) {
   auto client = factory->create_client();
   assert(client);

   auto ctx = std::make_shared<client_ctx<uint32_t>>(srv_id, factory);
   auto handler = static_cast<cstn::rpc_handler>([ctx] (cstn::ptr<cstn::resp_msg>& rsp,
                                 cstn::ptr<cstn::rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(srv_id);
   client->send(msg, handler);
   return ctx->future();
}

std::future<bool>
grpc_factory::client_request(shared<cstn::buffer> buf, shared<grpc_factory> factory) {
   auto client = factory->create_client();
   assert(client);

   auto ctx = std::make_shared<client_ctx<shared<cstn::buffer>>>(buf, factory);
   auto handler = static_cast<cstn::rpc_handler>([ctx] (cstn::ptr<cstn::resp_msg>& rsp,
                                 cstn::ptr<cstn::rpc_exception>& err) {
         respHandler(ctx, rsp, err);
      });

   auto msg = createMessage(buf);
   client->send(msg, handler);
   return ctx->future();
}

struct messaging_client : public cstn::grpc_client {
   messaging_client(shared<::grpc::ChannelInterface> channel, group_name_t const& grp_name) :
      cstn::grpc_client(),
      _stub(sds::messaging::Messaging::NewStub(channel)),
      _group_name(grp_name)
   {}

   ~messaging_client() override = default;

   ::grpc::Status send(::grpc::ClientContext *ctx,
                       raft_core::RaftMessage const &message,
                       raft_core::RaftMessage *response) override;

 private:
   std::unique_ptr<typename sds::messaging::Messaging::Stub> _stub;
   group_name_t const _group_name;
};

::grpc::Status
messaging_client::send(::grpc::ClientContext *ctx,
     raft_core::RaftMessage const &message,
     raft_core::RaftMessage *response) {
   sdsmsg::RaftGroupMsg group_msg;

   LOGTRACEMOD(sds_msg, "Sending [{}] from: [{}] to: [{}] Group: [{}]",
         message.base().type(),
         message.base().src(),
         message.base().dest(),
         _group_name);
   group_msg.set_group_name(_group_name);
   group_msg.mutable_message()->CopyFrom(message);

   sdsmsg::RaftGroupMsg group_rsp;
   auto status = _stub->RaftStep(ctx, group_msg, &group_rsp);
   response->CopyFrom(group_rsp.message());

   return status;
}


cstn::ptr<cstn::rpc_client>
grpc_factory::create_client() {
  auto endpoint = lookupEndpoint(std::to_string(current_leader()));
  LOGDEBUGMOD(sds_msg, "Creating client for [{}:{}] @ [{}]", current_leader(), _group_name, endpoint);
  return std::make_shared<messaging_client>(
      ::grpc::CreateChannel(endpoint,
        ::grpc::InsecureChannelCredentials()), _group_name
      );
}

cstn::ptr<cstn::rpc_client>
grpc_factory::create_client(const std::string &client) {
  auto endpoint = lookupEndpoint(client);
  LOGDEBUGMOD(sds_msg, "Creating client for [{}:{}] @ [{}]", client, _group_name, endpoint);
  return std::make_shared<messaging_client>(
      ::grpc::CreateChannel(endpoint,
        ::grpc::InsecureChannelCredentials()), _group_name
      );
}

}
