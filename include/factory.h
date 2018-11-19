#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <cornerstone/grpc_factory.hpp>
#include <sds_logging/logging.h>

SDS_LOGGING_DECL(sds_msg)

namespace cstn = ::cornerstone;

namespace sds_messaging {

using group_name_t = std::string;

template<typename T>
using shared = std::shared_ptr<T>;

struct grpc_factory : public cstn::grpc_factory {
   grpc_factory(group_name_t const& grp_id,
                uint32_t const current_leader) :
         cstn::grpc_factory(current_leader),
         _group_name(grp_id)
   { }

   cstn::ptr<cstn::rpc_client> create_client(const std::string &client) override;

   group_name_t group_name() const                { return _group_name; }

   virtual std::string lookupEndpoint(std::string const& client) = 0;

 private:
   group_name_t const _group_name;
};

}
