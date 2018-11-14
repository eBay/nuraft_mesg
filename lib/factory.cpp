
#include <grpcpp/create_channel.h>

#include "factory.h"
#include "messaging_service.grpc.pb.h"

namespace sds_messaging {

namespace sdsmsg = ::sds::messaging;

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
grpc_factory::create_client(const std::string &client) {
  auto endpoint = lookupEndpoint(client);
  LOGDEBUGMOD(sds_msg, "Creating client for [{}:{}] @ [{}]", client, _group_name, endpoint);
  return std::make_shared<messaging_client>(
      ::grpc::CreateChannel(endpoint,
        ::grpc::InsecureChannelCredentials()), _group_name
      );
}

}
