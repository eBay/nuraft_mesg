///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Messaging service routines
//

#include <sds_grpc/server.h>

#include "service.h"

SDS_LOGGING_DECL(sds_msg)

namespace sds::messaging {

using AsyncRaftSvc = Messaging::AsyncService;

msg_service::~msg_service() = default;

void msg_service::associate(::sds::grpc::GrpcServer* server) {
    RELEASE_ASSERT(server, "NULL server!");
    if (!server->register_async_service< Messaging >()) {
        LOGERRORMOD(nuraft, "Could not register RaftSvc with gRPC!");
        abort();
    }
}

void msg_service::bind(::sds::grpc::GrpcServer* server) {
    RELEASE_ASSERT(server, "NULL server!");
    if (!server->register_rpc< Messaging, RaftGroupMsg, RaftGroupMsg >(
            &AsyncRaftSvc::RequestRaftStep,
            std::bind(&msg_service::raftStep, this, std::placeholders::_1, std::placeholders::_2))) {
        LOGERRORMOD(nuraft, "Could not bind gRPC ::RaftStep to routine!");
        abort();
    }
}

::grpc::Status msg_service::raftStep(RaftGroupMsg& request, RaftGroupMsg& response) {
    auto const& group_name = request.group_name();

    auto const& base = request.message().base();
    LOGTRACEMOD(sds_msg, "Received [{}] from: [{}] to: [{}] Group: [{}]",
                nuraft::msg_type_to_string(nuraft::msg_type(base.type())), base.src(), base.dest(), group_name);

    if (nuraft::join_cluster_request == base.type()) {
        joinRaftGroup(base.dest(), group_name);
    }

    shared< sds::grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name); _raft_servers.end() != it) {
            COUNTER_INCREMENT(*it->second.m_metrics, group_steps, 1);
            server = it->second.m_server;
        }
    }

    response.set_group_name(group_name);
    if (server) {
        try {
            return server->step(*request.mutable_message(), *response.mutable_message());
        } catch (std::runtime_error& rte) { LOGERRORMOD(sds_msg, "Caught exception during step(): {}", rte.what()); }
    } else {
        LOGWARNMOD(sds_msg, "Missing RAFT group: {}", group_name);
    }
    return ::grpc::Status(::grpc::NOT_FOUND, "Missing RAFT group");
}

// We've hooked the gRPC server up to our "super-server", so we
// do not need to bind the grpc_servers to anything...just piggy-backing
// on their ::step() and transformations.
class null_service final : public sds::grpc_server {
public:
    using sds::grpc_server::grpc_server;
    void associate(sds::grpc::GrpcServer*) override{};
    void bind(sds::grpc::GrpcServer*) override{};
};

void msg_service::joinRaftGroup(int32_t const srv_id, group_name_t const& group_name) {
    LOGINFOMOD(sds_msg, "Joining RAFT group: {}", group_name);

    nuraft::context* ctx{nullptr};
    bool             happened{false};
    auto             it = _raft_servers.end();
    {
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        std::tie(it, happened) = _raft_servers.emplace(std::make_pair(group_name, group_name));
        if (_raft_servers.end() != it) {
            RELEASE_ASSERT(happened, "Could not insert new RAFT group.");
            if (happened) {
                if (auto err = _get_server_ctx(srv_id, group_name, ctx, it->second.m_metrics, this); err) {
                    LOGERRORMOD(sds_msg, "Error during RAFT server creation on group {}: {}", group_name,
                                err.message());
                    return;
                }
                RELEASE_ASSERT(ctx, "Could not retrieve RAFT context.");
            }
        }
    }
    if (_raft_servers.end() == it || !happened)
        return;

    auto server = std::make_shared< nuraft::raft_server >(ctx);
    it->second.m_server = std::make_shared< null_service >(server);
}

void msg_service::partRaftGroup(group_name_t const& group_name) {
    LOGINFOMOD(sds_msg, "Parting RAFT group: {}", group_name);
    shared< nuraft::raft_server > server;

    {
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name); _raft_servers.end() != it) {
            server = it->second.m_server->raft_server();
            _raft_servers.erase(it);
        } else {
            LOGWARNMOD(sds_msg, "Unknown RAFT group: {} cannot part.", group_name);
            return;
        }
    }

    server->shutdown();
}
} // namespace sds::messaging
