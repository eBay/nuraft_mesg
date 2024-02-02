#include "repl_service_ctx.hpp"

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/uuid/string_generator.hpp>
#include <sisl/grpc/generic_service.hpp>
#include <libnuraft/cluster_config.hxx>

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

    if (!_server) {
        LOGW("server not initialized");
        return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
    }

    if (std::holds_alternative< svr_id_t >(dest)) {
        if (auto const id_str = id_to_str(std::get< svr_id_t >(dest)); !id_str.empty()) {
            return boost::uuids::string_generator()(id_str);
        }
        return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
    }

    if (std::holds_alternative< role_regex >(dest)) {
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

const std::string& repl_service_ctx::raft_leader_id() const {
    // when adding member to raft,  the id recorded in raft is a hash
    // of passed-in id (new_id in add_member()), the new_id was stored
    // in endpoint field.
    static std::string const empty;
    if (!_server) return empty;
    if (auto leader = _server->get_srv_config(_server->get_leader()); nullptr != leader) {
        static_assert(std::is_reference< decltype(leader->get_endpoint()) >::value);
        return leader->get_endpoint();
    }
    return empty;
}

std::vector< peer_info > repl_service_ctx::get_raft_status() const {
    std::vector< peer_info > peers;
    if (!is_raft_leader()) return peers;
    if (!_server) return peers;

    auto pinfo_all = _server->get_peer_info_all();
    // add leader to the list
    nuraft::raft_server::peer_info pi_leader;
    pi_leader.id_ = _server->get_id();
    pi_leader.last_log_idx_ = _server->get_last_log_idx();
    pinfo_all.emplace_back(pi_leader);

    for (auto const& pinfo : pinfo_all) {
        std::string_view peer_id;
        if (auto srv_config = _server->get_srv_config(pinfo.id_); nullptr != srv_config) {
            peer_id = srv_config->get_endpoint();
        }
        DEBUG_ASSERT(!peer_id.empty(), "Unknown peer in config");
        peers.emplace_back(peer_info{std::string(peer_id), pinfo.last_log_idx_, pinfo.last_succ_resp_us_});
    }
    return peers;
}

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
