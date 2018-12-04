#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <cornerstone.hxx>
#include <sds_logging/logging.h>

#include "common.hpp"

SDS_LOGGING_DECL(raft_core)

namespace grpc {
struct ClientContext;
}

namespace raft_core {

class grpc_client;

class grpc_factory : public cstn::rpc_client_factory {
 public:
   explicit grpc_factory(uint32_t const current_leader) :
         rpc_client_factory(),
         _current_leader(current_leader)
   { }

   ~grpc_factory() override {
     std::lock_guard<std::mutex> lk(_client_lock);
     for (auto& client_pair : _clients) {
       client_pair.second.reset();
     }
     _clients.clear();
   }

   uint32_t current_leader() const                { std::lock_guard<std::mutex> lk(_leader_lock); return _current_leader; }
   void update_leader(uint32_t const leader)      { std::lock_guard<std::mutex> lk(_leader_lock); _current_leader = leader; }

   cstn::ptr<cstn::rpc_client>
   create_client(const std::string &client) override;

   virtual
   std::error_condition
   create_client(const std::string &client,
                 cstn::ptr<grpc_client>&) = 0;

   // Construct and send an AddServer message to the cluster
   static
   std::future<bool>
   add_server(uint32_t const srv_id, shared<grpc_factory> factory);

   // Send a client request to the cluster
   static
   std::future<bool>
   client_request(shared<cstn::buffer> buf, shared<grpc_factory> factory);

   // Construct and send a RemoveServer message to the cluster
   static
   std::future<bool>
   rem_server(uint32_t const srv_id, shared<grpc_factory> factory);

   // Send a pre-made message to the cluster
   static
   std::future<bool>
   cluster_request(shared<cstn::req_msg> msg, shared<grpc_factory> factory);

 private:
   mutable std::mutex _leader_lock;
   uint32_t           _current_leader;
   std::mutex _client_lock;
   std::map<std::string, std::shared_ptr<grpc_client>> _clients;
};

}
