#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <cornerstone.hxx>
#include <grpcpp/support/status.h>
#include <sds_logging/logging.h>

#include "raft_service.grpc.pb.h"

SDS_LOGGING_DECL(raft_core)

namespace grpc {
struct ClientContext;
}

namespace cornerstone {

template<typename T>
using shared = std::shared_ptr<T>;

struct grpc_client : public rpc_client {
   using rpc_client::rpc_client;

   virtual ::grpc::Status send(::grpc::ClientContext* ctx,
                               raft_core::RaftMessage const& message,
                               raft_core::RaftMessage* response) = 0;

   void send(ptr<req_msg>& req, rpc_handler& complete) override;
};

struct simple_grpc_client : public grpc_client {
   explicit simple_grpc_client(shared<::grpc::ChannelInterface> channel) :
      grpc_client(),
      _stub(raft_core::RaftSvc::NewStub(channel))
   {}

   ~simple_grpc_client() override = default;

   ::grpc::Status send(::grpc::ClientContext *ctx,
                       raft_core::RaftMessage const &message,
                       raft_core::RaftMessage *response) override;

 private:
   std::unique_ptr<typename raft_core::RaftSvc::Stub> _stub;
};


struct grpc_factory : public rpc_client_factory {
   explicit grpc_factory(uint32_t const current_leader) :
         rpc_client_factory(),
         _current_leader(current_leader)
   { }
   ~grpc_factory() override = default;

   uint32_t current_leader() const                { std::lock_guard<std::mutex> lk(_leader_lock); return _current_leader; }
   void update_leader(uint32_t const leader)      { std::lock_guard<std::mutex> lk(_leader_lock); _current_leader = leader; }

   // Construct and send an AddServer message to the cluster
   static
   std::future<bool>
   add_server(uint32_t const srv_id, shared<grpc_factory> factory);

   // Send a client request to the cluster
   static
   std::future<bool>
   client_request(shared<buffer> buf, shared<grpc_factory> factory);

   // Construct and send a RemoveServer message to the cluster
   static
   std::future<bool>
   rem_server(uint32_t const srv_id, shared<grpc_factory> factory);

   // Send a pre-made message to the cluster
   static
   std::future<bool>
   cluster_request(shared<req_msg> msg, shared<grpc_factory> factory);

 private:
   uint32_t           _current_leader;
   mutable std::mutex _leader_lock;
};

}
