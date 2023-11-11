#include <boost/uuid/string_generator.hpp>
#include <folly/Expected.h>
#include <grpcpp/impl/codegen/status_code_enum.h>
#include <libnuraft/async.hxx>
#include <libnuraft/rpc_listener.hxx>
#include <sisl/options/options.h>

#include "proto_service.hpp"

#include "messaging_service.grpc.pb.h"
#include "nuraft_mesg/mesg_factory.hpp"
#include "nuraft_mesg/nuraft_mesg.hpp"

SISL_OPTION_GROUP(nuraft_mesg,
                  (messaging_metrics, "", "msg_metrics", "Gather metrics from SD Messaging", cxxopts::value< bool >(),
                   ""))

#define CONTINUE_RESP(resp)                                                                                            \
    try {                                                                                                              \
        if (auto r = (resp)->get_result_code(); r != nuraft::RESULT_NOT_EXIST_YET) {                                   \
            if (nuraft::OK == r) return folly::Unit();                                                                 \
            return folly::makeUnexpected(r);                                                                           \
        }                                                                                                              \
        auto [p, sf] = folly::makePromiseContract< NullResult >();                                                     \
        (resp)->when_ready(                                                                                            \
            [p = std::make_shared< decltype(p) >(std::move(p))](                                                       \
                nuraft::cmd_result< nuraft::ptr< nuraft::buffer >, nuraft::ptr< std::exception > >& result,            \
                auto&) mutable {                                                                                       \
                if (nuraft::cmd_result_code::OK != result.get_result_code())                                           \
                    p->setValue(folly::makeUnexpected(result.get_result_code()));                                      \
                else                                                                                                   \
                    p->setValue(folly::Unit());                                                                        \
            });                                                                                                        \
        return std::move(sf);                                                                                          \
    } catch (std::runtime_error & rte) { LOGE("Caught exception: [group={}] {}", group_id, rte.what()); }

namespace nuraft_mesg {

using AsyncRaftSvc = Messaging::AsyncService;

grpc_server_wrapper::grpc_server_wrapper(group_id_t const& group_id) {
    if (0 < SISL_OPTIONS.count("msg_metrics")) m_metrics = std::make_shared< group_metrics >(group_id);
}

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
            "RaftStep", &AsyncRaftSvc::RequestRaftStep,
            std::bind(&proto_service::raftStep, this, std::placeholders::_1))) {
        LOGE("Could not bind gRPC ::RaftStep to routine!");
        abort();
    }
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

    // Find the RaftServer context based on the name of the group.
    std::shared_ptr< grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(gid); _raft_servers.end() != it) {
            if (it->second.m_metrics) COUNTER_INCREMENT(*it->second.m_metrics, group_steps, 1);
            server = it->second.m_server;
        }
    }

    // Setup our response and process the request. Group types are able to register a Callback that expects a Nullary
    // to process the requests and send back possibly asynchronous responses in a seperate context. This can be used
    // to offload the Raft append operations onto a seperate thread group.
    response.set_group_id(group_id);
    if (server) {
        /// TODO replace this ugly hack
        // if (auto offload = _get_process_offload(request.group_type()); nullptr != offload) {
        //     offload([rpc_data, server]() {
        //         auto& request = rpc_data->request();
        //         auto& response = rpc_data->response();
        //         rpc_data->set_status(server->step(request.msg(), *response.mutable_msg()));
        //         rpc_data->send_response();
        //     });
        //     return false;
        // }
        try {
            rpc_data->set_status(server->step(request.msg(), *response.mutable_msg()));
            return true;
        } catch (std::runtime_error& rte) { LOGE("Caught exception during step(): {}", rte.what()); }
    } else {
        LOGD("Missing [group={}]", group_id);
    }
    rpc_data->set_status(::grpc::Status(::grpc::NOT_FOUND, fmt::format("Missing RAFT group {}", group_id)));
    return true;
}

} // namespace nuraft_mesg
