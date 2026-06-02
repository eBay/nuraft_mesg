#pragma once

#include <list>
#include <memory>

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <libnuraft/state_mgr.hxx>
#include <libnuraft/callback.hxx>

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
class ManagerImpl;

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
    // The priority for leader election
    uint32_t priority_;
    // The peer is learner or not
    bool is_learner_;
    // The peer is new joiner or not
    bool is_new_joiner_;
};

class repl_service_ctx {
public:
    repl_service_ctx(nuraft::raft_server* server);
    virtual ~repl_service_ctx() = default;

    bool is_raft_leader() const;
    const std::string& raft_leader_id() const;
    std::vector< peer_info > get_raft_status() const;
    // Read-only access to the underlying raft server (valid for the owning group's lifetime). nuraft_mesg
    // is a binding over libnuraft and a consumer like homestore genuinely drives raft directly, so the
    // pointer is inherently exposed -- but as a const accessor, not a public mutable member: callers can no
    // longer reassign it (the old `_server = nullptr` foot-gun) and it reads clearly as "not owned here".
    nuraft::raft_server* raft_server() const { return _server; }

    // return a list of replica configs for the peers of the raft group
    void get_cluster_config(std::list< replica_config >& cluster_config) const;

    // data service api client calls (coroutine-native: co_await the returned task). params are taken BY
    // VALUE on purpose: these return lazy coroutines that may be stored and started later (e.g. a fan-out
    // collected into a vector then when_all'd), so reference params would dangle once the caller's
    // argument full-expression ends. By-value copies live in the coroutine frame.
    [[nodiscard]] virtual null_async_task data_service_request_unidirectional(destination_t dest,
                                                                            std::string request_name,
                                                                            io_blob_list_t cli_buf) = 0;
    [[nodiscard]] virtual async_task< sisl::GenericClientResponse >
    data_service_request_bidirectional(destination_t dest, std::string request_name, io_blob_list_t cli_buf) = 0;

    // Send response to a data service request and finish the async call.
    virtual void send_data_service_response(io_blob_list_t const& outgoing_buf,
                                            boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) = 0;

protected:
    // We do not own this pointer; it is valid for the lifetime of the owning raft group. Protected, not
    // public -- consumers use the accessors above rather than reaching through to the raw raft_server.
    nuraft::raft_server* _server;
};

class mesg_state_mgr : public nuraft::state_mgr {
public:
    using nuraft::state_mgr::state_mgr;
    virtual ~mesg_state_mgr() = default;

    // ----- consumer-implemented: the per-group state manager hooks -----
    virtual void become_ready() {}
    virtual uint32_t get_logstore_id() const = 0;
    virtual std::shared_ptr< nuraft::state_machine > get_state_machine() = 0;
    virtual void permanent_destroy() = 0;
    virtual void leave() = 0;
    virtual nuraft::cb_func::ReturnCode raft_event(nuraft::cb_func::Type, nuraft::cb_func::Param*) {
        return nuraft::cb_func::ReturnCode::Ok;
    }

    // The per-group session nuraft_mesg provides: data-service requests + raft access. Read-only accessor --
    // the consumer never holds a raw pointer and the unique_ptr stays owned here.
    repl_service_ctx* repl_ctx() const { return m_repl_svc_ctx.get(); }

protected:
    // Internal setup, invoked by nuraft_mesg (msg_service) when a group is wired up. Not part of the
    // consumer-facing interface; protected so a test double can re-expose it via a using-declaration.
    void make_repl_ctx(grpc_server* server, std::shared_ptr< mesg_factory > const& cli_factory);

private:
    // Internal wiring -- only nuraft_mesg drives these.
    friend class ManagerImpl;
    friend class msg_service;
    void set_manager_impl(std::weak_ptr< ManagerImpl > manager) { m_manager = manager; }
    nuraft::cb_func::ReturnCode internal_raft_event_handler(group_id_t const& group_id, nuraft::cb_func::Type type,
                                                            nuraft::cb_func::Param* param);

    std::unique_ptr< repl_service_ctx > m_repl_svc_ctx;
    std::weak_ptr< ManagerImpl > m_manager;
};

} // namespace nuraft_mesg
