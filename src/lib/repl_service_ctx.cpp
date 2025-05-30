#include "repl_service_ctx.hpp"

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/uuid/string_generator.hpp>
#include <sisl/grpc/generic_service.hpp>
#include <sisl/grpc/rpc_client.hpp>
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

AsyncResult< sisl::GenericClientResponse >
repl_service_ctx_grpc::data_service_request_bidirectional(destination_t const& dest, std::string const& request_name,
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
    if (!_server) return peers;

    if (is_raft_leader()) {
        // leader can get all the peer info
        auto pinfo_all = _server->get_peer_info_all();
        // get all the peer info except the leader
        for (auto const& pinfo : pinfo_all) {
            peer_info peer;
            if (auto srv_config = _server->get_srv_config(pinfo.id_); nullptr != srv_config) {
                peer.id_ = srv_config->get_endpoint();
                if (peer.id_.empty()) {
                    // if remove_member case , leader will first remove the member from peer_list, and then apply the
                    // new cluster configuraiton which excludes the removed member, so there is case that the peer_id is
                    // not in the config of leader.
                    LOGW("do not find peer id  {} in the conifg of leader", pinfo.id_);
                    continue;
                }
                // default priority=1
                peer.last_log_idx_ = pinfo.last_log_idx_;
                peer.last_succ_resp_us_ = pinfo.last_succ_resp_us_;
                peer.priority_ = srv_config->get_priority();
                peer.is_learner_ = srv_config->is_learner();
                peer.is_new_joiner_ = srv_config->is_new_joiner();
                peers.emplace_back(peer);
            } else {
                LOGW("do not find peer id  {} in the conifg of leader", pinfo.id_);
            }
        }
    }

    auto my_config = _server->get_srv_config(_server->get_id());

    // if I am the removed memeber, I can not find myself in the configuration. this will happen when a removed member
    // tries to get the raft status
    if (my_config) {
        auto my_peer_id = my_config->get_endpoint();

        // add the peer info of itself(leader or follower) , which is useful for upper layer
        // from the view of a node itself, last_succ_resp_us_ make no sense, so set it to 0
        peers.emplace_back(my_peer_id, _server->get_last_log_idx(), 0, my_config->get_priority(),
                           my_config->is_learner(), my_config->is_new_joiner());
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
    rpc_data->send_response(outgoing_buf);
}

} // namespace nuraft_mesg
