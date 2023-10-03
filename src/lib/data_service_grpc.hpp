#pragma once

#include <unordered_map>
#include <sisl/grpc/rpc_server.hpp>
#include <folly/SharedMutex.h>

#include "nuraft_mesg/nuraft_mesg.hpp"

namespace sisl {
struct io_blob;
}
namespace nuraft_mesg {

using data_lock_type = folly::SharedMutex;

class data_service_grpc {
    // key: group_id, value: map
    // value_key: request name, value_value: handler
    // Different groups can have same request name.
    std::unordered_map< std::string, sisl::generic_rpc_handler_cb_t > _request_map;
    data_lock_type _req_lock;

    // we do not own this pointer
    sisl::GrpcServer* _grpc_server;

public:
    data_service_grpc() = default;
    ~data_service_grpc() = default;
    data_service_grpc(data_service_grpc const&) = delete;
    data_service_grpc& operator=(data_service_grpc const&) = delete;

    void associate();
    void bind();
    bool bind(std::string const& request_name, group_id_t const& group_id,
              data_service_request_handler_t const& request_cb);

    void set_grpc_server(sisl::GrpcServer* server);
};

} // namespace nuraft_mesg
