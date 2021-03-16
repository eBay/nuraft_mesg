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

#include "grpcpp/impl/codegen/status_code_enum.h"
#include "libnuraft/async.hxx"
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

nuraft::cmd_result_code msg_service::add_srv(group_name_t const& group_name, nuraft::srv_config const& cfg) {
    shared< sds::grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name); _raft_servers.end() != it) {
            server = it->second.m_server;
        }
    }
    if (server) {
        try {
            return server->add_srv(cfg)->get_result_code();
        } catch (std::runtime_error& rte) { LOGERRORMOD(sds_msg, "Caught exception during step(): {}", rte.what()); }
    }
    return nuraft::SERVER_NOT_FOUND;
}

nuraft::cmd_result_code msg_service::rm_srv(group_name_t const& group_name, int const member_id) {
    shared< sds::grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name); _raft_servers.end() != it) {
            server = it->second.m_server;
        }
    }
    if (server) {
        try {
            return server->rem_srv(member_id)->get_result_code();
        } catch (std::runtime_error& rte) { LOGERRORMOD(sds_msg, "Caught exception during step(): {}", rte.what()); }
    }
    return nuraft::SERVER_NOT_FOUND;
}

nuraft::cmd_result_code msg_service::append_entries(group_name_t const& group_name,
                                                    std::vector< nuraft::ptr< nuraft::buffer > > const& logs) {
    shared< sds::grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name); _raft_servers.end() != it) {
            server = it->second.m_server;
        }
    }
    if (server) {
        try {
            return server->append_entries(logs)->get_result_code();
        } catch (std::runtime_error& rte) { LOGERRORMOD(sds_msg, "Caught exception during step(): {}", rte.what()); }
    }
    return nuraft::SERVER_NOT_FOUND;
}

::grpc::Status msg_service::raftStep(RaftGroupMsg& request, RaftGroupMsg& response) {
    auto const& group_name = request.group_name();
    auto const& intended_addr = request.intended_addr();

    // Verify this is for the service it was intended for
    auto const& base = request.message().base();
    if (intended_addr != _service_address) {
        LOGWARNMOD(sds_msg, "Recieved mesg for {} intended for {}, we are {}",
                   nuraft::msg_type_to_string(nuraft::msg_type(base.type())), intended_addr, _service_address);
        return ::grpc::Status(::grpc::INVALID_ARGUMENT, "Bad service address {}", intended_addr);
    }

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

std::error_condition msg_service::joinRaftGroup(int32_t const srv_id, group_name_t const& group_name) {
    LOGINFOMOD(sds_msg, "Joining RAFT group: {}", group_name);

    nuraft::context* ctx{nullptr};
    bool happened{false};
    auto it = _raft_servers.end();
    {
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        std::tie(it, happened) = _raft_servers.emplace(std::make_pair(group_name, group_name));
        if (_raft_servers.end() != it && happened) {
            if (auto err = _get_server_ctx(srv_id, group_name, ctx, it->second.m_metrics, this); err) {
                LOGERRORMOD(sds_msg, "Error during RAFT server creation on group {}: {}", group_name, err.message());
                return err;
            }
            auto server = std::make_shared< nuraft::raft_server >(ctx);
            it->second.m_server = std::make_shared< null_service >(server);
        }
    }
    return std::error_condition();
}

void msg_service::partRaftGroup(group_name_t const& group_name) {
    LOGINFOMOD(sds_msg, "Parting RAFT group: {}", group_name);
    shared< grpc_server > server;

    // FIXME: Today we only stop the gRPC server, we do not destroy it...leaking it until
    // the next restart of the process. This is to solve SDSTOR-2341 when we get
    // responses on a gRPC client for a server tha no longer exists. All threads are stopped,
    // I believe this to cause minimal leakage.
    {
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_name); _raft_servers.end() != it) {
            server = it->second.m_server;
        } else {
            LOGWARNMOD(sds_msg, "Unknown RAFT group: {} cannot part.", group_name);
            return;
        }
    }
    if (auto raft_server = server->raft_server(); raft_server) {
        raft_server->stop_server();
        raft_server->shutdown();
    }
}
} // namespace sds::messaging
