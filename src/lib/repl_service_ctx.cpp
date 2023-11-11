#include "repl_service_ctx.hpp"

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/uuid/string_generator.hpp>
#include <sisl/grpc/generic_service.hpp>

#include "nuraft_mesg/mesg_factory.hpp"
#include "grpc_server.hpp"
#include "common_lib.hpp"

namespace nuraft_mesg {

// The endpoint field of the raft_server config is the uuid of the server.
const std::string repl_service_ctx_grpc::id_to_str(int32_t const id) const {
    auto const& srv_config = _server->get_config()->get_server(id);
    return (srv_config) ? srv_config->get_endpoint() : std::string();
}

const std::optional< Result< peer_id_t > > repl_service_ctx_grpc::get_peer_id(destination_t const& dest) const {
    if (std::holds_alternative< peer_id_t >(dest)) return std::get< peer_id_t >(dest);
    if (std::holds_alternative< role_regex >(dest)) {
        if (!_server) {
            LOGW("server not initialized");
            return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
        }
        switch (std::get< role_regex >(dest)) {
        case role_regex::LEADER: {
            if (is_raft_leader()) return folly::makeUnexpected(nuraft::cmd_result_code::BAD_REQUEST);
            auto const leader = _server->get_leader();
            if (leader == -1) return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
            return boost::uuids::string_generator()(id_to_str(leader));
        } break;
        case role_regex::ALL: {
            return std::nullopt;
        } break;
        default: {
            LOGE("Method not implemented");
            return folly::makeUnexpected(nuraft::cmd_result_code::BAD_REQUEST);
        } break;
        }
    }
    DEBUG_ASSERT(false, "Unknown destination type");
    return folly::makeUnexpected(nuraft::cmd_result_code::BAD_REQUEST);
}

NullAsyncResult repl_service_ctx_grpc::data_service_request_unidirectional(destination_t const& dest,
                                                                           std::string const& request_name,
                                                                           io_blob_list_t const& cli_buf) {
    return (!m_mesg_factory)
        ? folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND)
        : m_mesg_factory->data_service_request_unidirectional(get_peer_id(dest), request_name, cli_buf);
}

AsyncResult< sisl::io_blob > repl_service_ctx_grpc::data_service_request_bidirectional(destination_t const& dest,
                                                                                       std::string const& request_name,
                                                                                       io_blob_list_t const& cli_buf) {
    return (!m_mesg_factory)
        ? folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND)
        : m_mesg_factory->data_service_request_bidirectional(get_peer_id(dest), request_name, cli_buf);
}

repl_service_ctx::repl_service_ctx(nuraft::raft_server* server) : _server(server) {}

bool repl_service_ctx::is_raft_leader() const { return _server->is_leader(); }

void repl_service_ctx::get_cluster_config(std::list< replica_config >& cluster_config) const {
    auto const& srv_configs = _server->get_config()->get_servers();
    for (auto const& srv_config : srv_configs) {
        cluster_config.emplace_back(replica_config{srv_config->get_endpoint(), srv_config->get_aux()});
    }
}

repl_service_ctx_grpc::repl_service_ctx_grpc(grpc_server* server, std::shared_ptr< mesg_factory > const& cli_factory) :
        repl_service_ctx(server ? server->raft_server().get() : nullptr), m_mesg_factory(cli_factory) {}

void repl_service_ctx_grpc::send_data_service_response(io_blob_list_t const& outgoing_buf,
                                                       boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) {
    serialize_to_byte_buffer(rpc_data->response(), outgoing_buf);
    rpc_data->send_response();
}

} // namespace nuraft_mesg
