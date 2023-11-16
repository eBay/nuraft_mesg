#include <boost/uuid/string_generator.hpp>
#include <folly/Expected.h>
#include <grpcpp/impl/codegen/status_code_enum.h>
#include <libnuraft/async.hxx>
#include <libnuraft/rpc_listener.hxx>
#include <sisl/options/options.h>

#include "nuraft_mesg/mesg_factory.hpp"
#include "nuraft_mesg/nuraft_mesg.hpp"

#include "lib/service.hpp"

#include "messaging_service.grpc.pb.h"
#include "utils.hpp"

namespace nuraft_mesg {

static std::shared_ptr< nuraft::req_msg > toRequest(RaftMessage const& raft_msg) {
    assert(raft_msg.has_rc_request());
    auto const& base = raft_msg.base();
    auto const& req = raft_msg.rc_request();
    auto message =
        std::make_shared< nuraft::req_msg >(base.term(), (nuraft::msg_type)base.type(), base.src(), base.dest(),
                                            req.last_log_term(), req.last_log_index(), req.commit_index());
    auto& log_entries = message->log_entries();
    for (auto const& log : req.log_entries()) {
        auto log_buffer = nuraft::buffer::alloc(log.buffer().size());
        memcpy(log_buffer->data(), log.buffer().data(), log.buffer().size());
        log_entries.push_back(std::make_shared< nuraft::log_entry >(log.term(), log_buffer,
                                                                    (nuraft::log_val_type)log.type(), log.timestamp()));
    }
    return message;
}

static RCResponse* fromRCResponse(nuraft::resp_msg& rcmsg) {
    auto req = new RCResponse;
    req->set_next_index(rcmsg.get_next_idx());
    req->set_accepted(rcmsg.get_accepted());
    req->set_result_code((ResultCode)(0 - rcmsg.get_result_code()));
    auto ctx = rcmsg.get_ctx();
    if (ctx) { req->set_context(ctx->data(), ctx->container_size()); }
    return req;
}

class proto_service : public msg_service {
    ::grpc::Status step(nuraft::raft_server& server, const RaftMessage& request, RaftMessage& reply);

public:
    using msg_service::msg_service;
    void associate(sisl::GrpcServer* server) override;
    void bind(sisl::GrpcServer* server) override;

    // Incomming gRPC message
    bool raftStep(const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data);
};

void proto_service::associate(::sisl::GrpcServer* server) {
    msg_service::associate(server);
    if (!server->register_async_service< Messaging >()) {
        LOGE("Could not register RaftSvc with gRPC!");
        abort();
    }
}

void proto_service::bind(::sisl::GrpcServer* server) {
    msg_service::bind(server);
    if (!server->register_rpc< Messaging, RaftGroupMsg, RaftGroupMsg, false >(
            "RaftStep", &Messaging::AsyncService::RequestRaftStep,
            std::bind(&proto_service::raftStep, this, std::placeholders::_1))) {
        LOGE("Could not bind gRPC ::RaftStep to routine!");
        abort();
    }
}

::grpc::Status proto_service::step(nuraft::raft_server& server, const RaftMessage& request, RaftMessage& reply) {
    LOGT("Stepping [{}] from: [{}] to: [{}]", nuraft::msg_type_to_string(nuraft::msg_type(request.base().type())),
         request.base().src(), request.base().dest());
    auto rcreq = toRequest(request);
    auto resp = nuraft::raft_server_handler::process_req(&server, *rcreq);
    if (!resp) { return ::grpc::Status(::grpc::StatusCode::CANCELLED, "Server rejected request"); }
    assert(resp);
    reply.set_allocated_base(fromBaseRequest(*resp));
    reply.set_allocated_rc_response(fromRCResponse(*resp));
    if (!resp->get_accepted()) {
        auto const srv_conf = server.get_srv_config(reply.base().dest());
        if (srv_conf) { reply.mutable_rc_response()->set_dest_addr(srv_conf->get_endpoint()); }
    }
    return ::grpc::Status();
}

bool proto_service::raftStep(const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data) {
    auto& request = rpc_data->request();
    auto& response = rpc_data->response();
    auto const& group_id = request.group_id();
    auto const& intended_addr = request.intended_addr();

    auto gid = boost::uuids::uuid();
    auto sid = boost::uuids::uuid();
    try {
        gid = boost::uuids::string_generator()(group_id);
        sid = boost::uuids::string_generator()(intended_addr);
    } catch (std::runtime_error const& e) {
        LOGW("Recieved mesg for [group={}] [addr={}] which is not a valid UUID!", group_id, intended_addr);
        rpc_data->set_status(
            ::grpc::Status(::grpc::INVALID_ARGUMENT, fmt::format(FMT_STRING("Bad GroupID {}"), group_id)));
        return true;
    }

    // Verify this is for the service it was intended for
    auto const& base = request.msg().base();
    if (sid != _service_address) {
        LOGW("Recieved mesg for {} intended for {}, we are {}",
             nuraft::msg_type_to_string(nuraft::msg_type(base.type())), intended_addr, _service_address);
        rpc_data->set_status(::grpc::Status(
            ::grpc::INVALID_ARGUMENT,
            fmt::format(FMT_STRING("intended addr: [{}], our addr: [{}]"), intended_addr, _service_address)));
        return true;
    }

    LOGT("Received [{}] from: [{}] to: [{}] Group: [{}]", nuraft::msg_type_to_string(nuraft::msg_type(base.type())),
         base.src(), base.dest(), group_id);

    // JoinClusterRequests are expected to be received upon Cluster creation by the current leader. We need
    // to initialize a RaftServer context based on the corresponding type prior to servicing this request. This
    // should emplace a corresponding server in the _raft_servers member.
    if (nuraft::join_cluster_request == base.type()) { joinRaftGroup(base.dest(), gid, request.group_type()); }

    // Setup our response and process the request.
    response.set_group_id(group_id);
    if (auto it = _raft_servers.find(gid); _raft_servers.end() != it) {
        if (it->second.m_metrics) COUNTER_INCREMENT(*it->second.m_metrics, group_steps, 1);
        try {
            rpc_data->set_status(step(*it->second.m_server->raft_server(), request.msg(), *response.mutable_msg()));
            return true;
        } catch (std::runtime_error& rte) { LOGE("Caught exception during step(): {}", rte.what()); }
    } else {
        LOGD("Missing [group={}]", group_id);
    }
    rpc_data->set_status(::grpc::Status(::grpc::NOT_FOUND, fmt::format("Missing RAFT group {}", group_id)));
    return true;
}

std::shared_ptr< msg_service > msg_service::create(std::shared_ptr< ManagerImpl > const& manager,
                                                   group_id_t const& service_address,
                                                   std::string const& default_group_type,
                                                   bool const enable_data_service) {
    return std::make_shared< proto_service >(manager, service_address, default_group_type, enable_data_service);
}

} // namespace nuraft_mesg
