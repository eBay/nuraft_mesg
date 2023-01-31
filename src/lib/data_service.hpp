#pragma once

#include <map>
#include <sisl/grpc/rpc_server.hpp>
#include <folly/SharedMutex.h>

namespace sisl {
struct io_blob;
}
namespace nuraft_mesg {

using data_lock_type = folly::SharedMutex;
using data_service_request_handler_t = std::function< bool(sisl::io_blob const& incoming_buf) >;

class data_service {
    std::map< std::string, sisl::generic_rpc_handler_cb_t > _request_map;
    data_lock_type _req_lock;

public:
    data_service() = default;
    ~data_service() = default;
    data_service(data_service const&) = delete;
    data_service& operator=(data_service const&) = delete;

    void associate(sisl::GrpcServer* server);
    void bind(sisl::GrpcServer* server);
    bool bind(sisl::GrpcServer* server, std::string const& request_name,
              data_service_request_handler_t const& request_cb);
};

} // namespace nuraft_mesg
