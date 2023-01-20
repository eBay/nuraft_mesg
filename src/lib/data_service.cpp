#include <sisl/grpc/generic_service.hpp>
#include "data_service.h"
#include "utils.hpp"

namespace nuraft_mesg {

void data_service::associate(sisl::GrpcServer* server) {
    RELEASE_ASSERT(server, "NULL server!");
    if (!server->register_async_generic_service()) {
        throw std::runtime_error("Could not register generic service with gRPC!");
    }
}

void data_service::bind(sisl::GrpcServer* server) {
    RELEASE_ASSERT(server, "NULL server!");
    auto lk = std::unique_lock< std::mutex >(_req_lock);
    for (auto const& [request_name, request_cb] : _request_map) {
        if (!server->register_generic_rpc(request_name, request_cb)) {
            throw std::runtime_error(fmt::format("Could not register generic rpc {} with gRPC!", request_name));
        }
    }
}

void data_service::bind(sisl::GrpcServer* server, std::string const& request_name,
                        data_service_request_handler_t const& request_cb) {
    RELEASE_ASSERT(server, "NULL server!");
    auto generic_handler_cb = [request_cb](boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) {
        std::vector< sisl::io_blob > svr_buf;
        deserializeFromByteBuffer(rpc_data->request(), svr_buf);
        return request_cb(svr_buf);
    };
    auto lk = std::unique_lock< std::mutex >(_req_lock);
    auto [it, happened] = _request_map.emplace(request_name, generic_handler_cb);
    if (it != _request_map.end()) {
        if (happened) {
            if (!server->register_generic_rpc(it->first, it->second)) {
                throw std::runtime_error(fmt::format("Could not register generic rpc {} with gRPC!", request_name));
            }
        } else {
            LOGWARN("data service rpc {} exists", it->first);
        }
    } else {
        throw std::runtime_error(
            fmt::format("Could not register generic rpc {} with gRPC! Not enough memory.", request_name));
    }
}

} // namespace nuraft_mesg