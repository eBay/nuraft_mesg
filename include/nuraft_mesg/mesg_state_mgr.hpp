#pragma once

#include <list>
#include <memory>

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <libnuraft/state_mgr.hxx>

#include "common.hpp"

namespace nuraft {
class raft_server;
class state_machine;
} // namespace nuraft

namespace sisl {
class GenericRpcData;
} // namespace sisl

namespace nuraft_mesg {

class mesg_factory;
class grpc_server;

// config for a replica with after the int32_t id is transformed to a peer_id_t
struct replica_config {
    std::string peer_id;
    std::string aux;
};

struct peer_info {
    // Peer ID.
    std::string id_;
    // The last log index that the peer has, from this server's point of view.
    uint64_t last_log_idx_;
    // The elapsed time since the last successful response from this peer, set to 0 on leader
    uint64_t last_succ_resp_us_;
};

class repl_service_ctx {
public:
    repl_service_ctx(nuraft::raft_server* server);
    virtual ~repl_service_ctx() = default;

    // we do not own this pointer. Use this only if the life cycle of the pointer is well known
    nuraft::raft_server* _server;
    bool is_raft_leader() const;
    const std::string& raft_leader_id() const;
    std::vector< peer_info > get_raft_status() const;

    // return a list of replica configs for the peers of the raft group
    void get_cluster_config(std::list< replica_config >& cluster_config) const;

    // data service api client calls
    virtual NullAsyncResult data_service_request_unidirectional(destination_t const& dest,
                                                                std::string const& request_name,
                                                                io_blob_list_t const& cli_buf) = 0;
    virtual AsyncResult< sisl::GenericClientResponse >
    data_service_request_bidirectional(destination_t const& dest, std::string const& request_name,
                                       io_blob_list_t const& cli_buf) = 0;

    // Send response to a data service request and finish the async call.
    virtual void send_data_service_response(io_blob_list_t const& outgoing_buf,
                                            boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) = 0;
};

class mesg_state_mgr : public nuraft::state_mgr {
public:
    using nuraft::state_mgr::state_mgr;
    virtual ~mesg_state_mgr() = default;
    void make_repl_ctx(grpc_server* server, std::shared_ptr< mesg_factory > const& cli_factory);

    virtual void become_ready() {}
    virtual uint32_t get_logstore_id() const = 0;
    virtual std::shared_ptr< nuraft::state_machine > get_state_machine() = 0;
    virtual void permanent_destroy() = 0;
    virtual void leave() = 0;

protected:
    std::unique_ptr< repl_service_ctx > m_repl_svc_ctx;
};

} // namespace nuraft_mesg
