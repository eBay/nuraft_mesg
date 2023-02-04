#include <sisl/grpc/generic_service.hpp>
#include "data_service_grpc.hpp"
#include "utils.hpp"

SISL_LOGGING_DECL(nuraft_mesg)

namespace nuraft_mesg {

void data_service_grpc::set_grpc_server(sisl::GrpcServer* server) { _grpc_server = server; }

void data_service_grpc::associate() {
    if (!_grpc_server->register_async_generic_service()) {
        throw std::runtime_error("Could not register generic service with gRPC!");
    }
}

void data_service_grpc::bind() {
    auto lk = std::unique_lock< data_lock_type >(_req_lock);
    for (auto const& [request_name, request_cb] : _request_map) {
        if (!_grpc_server->register_generic_rpc(request_name, request_cb)) {
            throw std::runtime_error(fmt::format("Could not register generic rpc {} with gRPC!", request_name));
        }
    }
}

bool data_service_grpc::bind(std::string const& request_name, std::string const& group_id,
                             data_service_request_handler_t const& request_cb) {
    RELEASE_ASSERT(_grpc_server, "NULL _grpc_server!");
    if (!request_cb) {
        LOGWARNMOD(nuraft_mesg, "request_cb null for the request {}, cannot bind.", request_name);
        return false;
    }
    auto generic_handler_cb = [request_cb](boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) {
        sisl::io_blob svr_buf;
        if (auto status = deserialize_from_byte_buffer(rpc_data->request(), svr_buf); !status.ok()) {
            LOGERRORMOD(nuraft_mesg, "ByteBuffer DumpToSingleSlice failed, {}", status.error_message());
            rpc_data->set_status(status);
            return true; // respond immediately
        }
        return request_cb(svr_buf);
    };
    auto lk = std::unique_lock< data_lock_type >(_req_lock);
    auto [it, happened] = _request_map.emplace(get_generic_method_name(request_name, group_id), generic_handler_cb);
    if (it != _request_map.end()) {
        if (happened) {
            if (!_grpc_server->register_generic_rpc(it->first, it->second)) {
                throw std::runtime_error(fmt::format("Could not register generic rpc {} with gRPC!", request_name));
            }
        } else {
            LOGWARNMOD(nuraft_mesg, "data service rpc {} exists", it->first);
            return false;
        }
    } else {
        throw std::runtime_error(
            fmt::format("Could not register generic rpc {} with gRPC! Not enough memory.", request_name));
    }
    return true;
}

} // namespace nuraft_mesg