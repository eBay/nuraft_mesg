#pragma once

#include <unordered_map>
#include <sisl/grpc/rpc_server.hpp>
#include <folly/SharedMutex.h>
#include "data_service.hpp"

namespace sisl {
struct io_blob;
}
namespace nuraft_mesg {

using data_lock_type = folly::SharedMutex;

class data_service_grpc : public data_service {
    // key: group_id, value: map
    // value_key: request name, value_value: handler
    // Different groups can have same request name.
    std::unordered_map< std::string, std::pair< sisl::generic_rpc_handler_cb_t, sisl::generic_rpc_completed_cb_t > >
        _request_map;
    data_lock_type _req_lock;

    // we do not own this pointer
    sisl::GrpcServer* _grpc_server;

public:
    data_service_grpc() = default;
    ~data_service_grpc() = default;
    data_service_grpc(data_service_grpc const&) = delete;
    data_service_grpc& operator=(data_service_grpc const&) = delete;

    void associate() override;
    void bind() override;
    bool bind(std::string const& request_name, std::string const& group_id,
              data_service_request_handler_t const& request_cb, data_service_comp_handler_t const& comp_cb) override;

    void set_grpc_server(sisl::GrpcServer* server);
};

} // namespace nuraft_mesg
