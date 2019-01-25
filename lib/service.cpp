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

namespace sds::messaging {

using AsyncRaftSvc = Messaging::AsyncService;

msg_service::~msg_service() = default;

void
msg_service::associate(::sds::grpc::GrpcServer* server) {
        assert(server);
        if (!server->register_async_service<Messaging>()) {
            LOGERRORMOD(raft_core, "Could not register RaftSvc with gRPC!");
            abort();
        }
}

void
msg_service::bind(::sds::grpc::GrpcServer* server) {
        assert(server);
        if (!server->register_rpc<Messaging, RaftGroupMsg, RaftGroupMsg>(
            &AsyncRaftSvc::RequestRaftStep,
            std::bind(&msg_service::raftStep, this, std::placeholders::_1, std::placeholders::_2))) {
            LOGERRORMOD(raft_core, "Could not bind gRPC ::RaftStep to routine!");
            abort();
        }
}

::grpc::Status
msg_service::raftStep(RaftGroupMsg& request, RaftGroupMsg& response) {
    auto const &group_name = request.group_name();

    auto const& base = request.message().base();
    LOGTRACEMOD(sds_msg, "Received [{}] from: [{}] to: [{}] Group: [{}]",
            cornerstone::msg_type_to_string(cornerstone::msg_type(base.type())),
            base.src(),
            base.dest(),
            group_name
            );

    if (cornerstone::join_cluster_request == base.type()) {
        joinRaftGroup(base.dest(), group_name);
    }

    shared<raft_core::grpc_server> server;
    {
        std::shared_lock<lock_type> rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name);
                _raft_servers.end() != it) {
            server = it->second;
        }
    }

    response.set_group_name(group_name);
    if (server) {
        try {
            return server->step(*request.mutable_message(), *response.mutable_message());
        } catch (std::runtime_error& rte) {
            LOGERRORMOD(sds_msg, "Caught exception during step(): {}", rte.what());
        }
    } else {
        LOGWARNMOD(sds_msg, "Missing RAFT group: {}", group_name);
    }
    return ::grpc::Status(::grpc::NOT_FOUND, "Missing RAFT group");
}

// We've hooked the gRPC server up to our "super-server", so we
// do not need to bind the grpc_servers to anything...just piggy-backing
// on their ::step() and transformations.
class null_service final : public raft_core::grpc_server {
 public:
    using raft_core::grpc_server::grpc_server;
    void associate(sds::grpc::GrpcServer*) override {};
    void bind(sds::grpc::GrpcServer*) override {};
};

void
msg_service::joinRaftGroup(int32_t const srv_id, group_name_t const& group_name) {
    LOGINFOMOD(sds_msg, "Joining RAFT group: {}", group_name);

    std::unique_lock<lock_type> lck(_raft_servers_lock);
    auto [it, happened] = _raft_servers.emplace(std::make_pair(group_name, nullptr));
    if (_raft_servers.end() == it || !happened) return;

    cornerstone::context* ctx {nullptr};
    if (auto err = _get_server_ctx(srv_id, group_name, ctx, this); err) {
        LOGERRORMOD(sds_msg,
                    "Error during RAFT server creation on group {}: {}",
                    group_name,
                    err.message());
        return;
    }
    assert(ctx != nullptr);

    auto server = std::make_shared<cornerstone::raft_server>(ctx);
    it->second = std::make_shared<null_service>(server);
}

void
msg_service::partRaftGroup(group_name_t const& group_name) {
    LOGINFOMOD(sds_msg, "Parting RAFT group: {}", group_name);
    shared<cornerstone::raft_server> server;

    {   std::unique_lock<lock_type> lck(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name); _raft_servers.end() != it) {
            server = it->second->raft_server();
            _raft_servers.erase(it);
        } else {
            LOGWARNMOD(sds_msg, "Unknown RAFT group: {} cannot part.", group_name);
            return;
        }
    }

    server->shutdown();
}
}
