#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <raft_core_grpc/grpc_factory.hpp>
#include <sds_logging/logging.h>

SDS_LOGGING_DECL(sds_msg)

namespace cstn = ::cornerstone;

namespace sds::messaging {

using group_name_t = std::string;

class mesg_client;

template<typename T>
using shared = std::shared_ptr<T>;

class group_factory : public raft_core::grpc_factory {
 public:
   using raft_core::grpc_factory::grpc_factory;

   using raft_core::grpc_factory::create_client;

   std::error_condition
   create_client(const std::string &client, cstn::ptr<cstn::rpc_client>&) override;

   std::error_condition
   reinit_client(raft_core::shared<cornerstone::rpc_client>& raft_client) override;

   virtual std::string lookupEndpoint(std::string const& client) = 0;
};

class mesg_factory final : public raft_core::grpc_factory {
   shared<group_factory> _group_factory;
   group_name_t const    _group_name;

 public:
   mesg_factory(shared<group_factory> g_factory,
                group_name_t const& grp_id,
                uint32_t const current_leader) :
         raft_core::grpc_factory(current_leader, 0, grp_id),
         _group_factory(g_factory),
         _group_name(grp_id)
   { }

   group_name_t group_name() const { return _group_name; }

   std::error_condition
   create_client(const std::string &client, cstn::ptr<cstn::rpc_client>& rpc_ptr) override;

   std::string
   lookup_address(int32_t srv_id) override;

   std::error_condition
   reinit_client(raft_core::shared<cornerstone::rpc_client>& raft_client) override;
};


}
