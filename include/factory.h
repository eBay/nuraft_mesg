#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <cornerstone/raft_core_grpc.hpp>
#include <sds_logging/logging.h>

SDS_LOGGING_DECL(sds_msg)

namespace sds_messaging {

namespace cstn = ::cornerstone;

using group_name_t = std::string;

template<typename T>
using shared = std::shared_ptr<T>;

struct grpc_factory : public cstn::rpc_client_factory {
   grpc_factory(group_name_t const& grp_id,
                uint32_t const current_leader) :
         cstn::rpc_client_factory(),
         _group_name(grp_id),
         _current_leader(current_leader)
   { }

   cstn::ptr<cstn::rpc_client> create_client();
   cstn::ptr<cstn::rpc_client> create_client(const std::string &client) override;

   group_name_t group_name() const                { return _group_name; }
   uint32_t current_leader() const                { std::lock_guard<std::mutex> lk(_leader_lock); return _current_leader; }
   void update_leader(uint32_t const leader)      { std::lock_guard<std::mutex> lk(_leader_lock); _current_leader = leader; }

   virtual std::string lookupEndpoint(std::string const& client) = 0;

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
   group_name_t const _group_name;
   uint32_t           _current_leader;
   mutable std::mutex _leader_lock;
};

}
