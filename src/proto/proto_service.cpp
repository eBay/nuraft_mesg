#include <boost/uuid/string_generator.hpp>
#include <folly/Expected.h>
#include <grpcpp/impl/codegen/status_code_enum.h>
#include <boost/asio.hpp>
#include <libnuraft/async.hxx>
#include <libnuraft/rpc_listener.hxx>
#include <sisl/options/options.h>

#include "nuraft_mesg/mesg_factory.hpp"
#include "nuraft_mesg/nuraft_mesg.hpp"

#include "lib/service.hpp"
#include "lib/nuraft_mesg_config.hpp"

#include "messaging_service.grpc.pb.h"
#include "utils.hpp"

namespace nuraft_mesg {

// Service-level metrics (global to the service, not per-group)
class service_metrics : public sisl::MetricsGroupWrapper {
public:
    service_metrics() : sisl::MetricsGroupWrapper("RAFTService", "global") {
        REGISTER_HISTOGRAM(raft_pool_wait_time_us, "Time waiting in raft thread pool queue", "raft_service_latency",
                           {"op", "raft_pool_wait"});
        REGISTER_HISTOGRAM(io_pool_wait_time_us, "Time waiting in I/O thread pool queue", "raft_service_latency",
                           {"op", "io_pool_wait"});
        REGISTER_GAUGE(raft_pool_active_threads, "Number of active threads in Raft pool", "raft_service_gauge");
        REGISTER_GAUGE(io_pool_active_threads, "Number of active threads in I/O pool", "raft_service_gauge");
        REGISTER_COUNTER(raft_pool_msg_count, "Messages processed on Raft thread", "raft_service_counter");
        REGISTER_COUNTER(io_pool_msg_count, "Messages routed to I/O pool", "raft_service_counter");
        register_me_to_farm();
    }

    ~service_metrics() { deregister_me_from_farm(); }
};

inline int64_t get_elapsed_time_us(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast< std::chrono::microseconds >(std::chrono::steady_clock::now() - start).count();
}

// Simple RAII guard for atomic counter
struct atomic_counter_guard {
    std::atomic< int >& counter;
    explicit atomic_counter_guard(std::atomic< int >& c) : counter(c) { ++counter; }
    ~atomic_counter_guard() { --counter; }
    atomic_counter_guard(const atomic_counter_guard&) = delete;
    atomic_counter_guard& operator=(const atomic_counter_guard&) = delete;
};

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
    req->set_batch_size_hint(rcmsg.get_next_batch_size_hint_in_bytes());
    req->set_result_code((ResultCode)(0 - rcmsg.get_result_code()));
    auto ctx = rcmsg.get_ctx();
    if (ctx) { req->set_context(ctx->data(), ctx->size()); }
    return req;
}

class proto_service : public msg_service {
    ::grpc::Status step(nuraft::raft_server& server, const RaftMessage& request, RaftMessage& reply,
                        std::shared_ptr< group_metrics > metrics);

public:
    template < typename... Args >
    proto_service(Args&&... args) :
            msg_service(std::forward< Args >(args)...),
            _raft_thread_pool{NURAFT_MESG_CONFIG(raft_append_entries_thread_cnt)},
            _io_thread_pool{calculate_io_pool_size()} {}

    void associate(sisl::GrpcServer* server) override;
    void bind(sisl::GrpcServer* server) override;

    // Incomming gRPC message
    bool raftStep(const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data);

private:
    size_t calculate_io_pool_size();
    bool is_slow_message(nuraft::msg_type type) const;
    int64_t execute_step(std::shared_ptr< nuraft::raft_server > const& server,
                         const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data,
                         std::shared_ptr< group_metrics > const& metrics);

    boost::asio::thread_pool _raft_thread_pool;
    boost::asio::thread_pool _io_thread_pool;
    std::atomic< int > _raft_pool_active_threads{0};
    std::atomic< int > _io_pool_active_threads{0};
    service_metrics _service_metrics;
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

size_t proto_service::calculate_io_pool_size() {
    auto config_size = NURAFT_MESG_CONFIG(io_thread_pool_size);
    return (config_size > 0) ? config_size : 4;  // Default to 4 threads
}

bool proto_service::is_slow_message(nuraft::msg_type type) const {
    return type == nuraft::msg_type::append_entries_request ||
           type == nuraft::msg_type::install_snapshot_request;
}

int64_t proto_service::execute_step(std::shared_ptr< nuraft::raft_server > const& server,
                                     const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data,
                                     std::shared_ptr< group_metrics > const& metrics) {
    auto& request = rpc_data->request();
    auto& response = rpc_data->response();
    auto const& group_id = request.group_id();

    auto exec_start = std::chrono::steady_clock::now();

    try {
        response.set_group_id(group_id);
        rpc_data->set_status(step(*server, request.msg(), *response.mutable_msg(), metrics));
    } catch (std::runtime_error& rte) {
        LOGE("Caught exception during step(): {}", rte.what());
        rpc_data->set_status(::grpc::Status(::grpc::NOT_FOUND,
            fmt::format("Missing RAFT group {}", group_id)));
    }

    rpc_data->send_response();

    return get_elapsed_time_us(exec_start);
}

::grpc::Status proto_service::step(nuraft::raft_server& server, const RaftMessage& request, RaftMessage& reply,
                                   std::shared_ptr< group_metrics > metrics) {
    LOGT("Stepping [{}] from: [{}] to: [{}]", nuraft::msg_type_to_string(nuraft::msg_type(request.base().type())),
         request.base().src(), request.base().dest());
    auto rcreq = toRequest(request);
    auto const time_start = std::chrono::steady_clock::now();
    auto resp = nuraft::raft_server_handler::process_req(&server, *rcreq);
    if (!resp) { return ::grpc::Status(::grpc::StatusCode::CANCELLED, "Server rejected request"); }
    if (metrics && rcreq->get_type() == nuraft::msg_type::append_entries_request) {
        HISTOGRAM_OBSERVE(*metrics, append_entries_latency_us, get_elapsed_time_us(time_start));
    }
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
        LOGW("Recieved mesg for [{}:{}] intended for {}, we are {}", group_id,
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

    auto raft_post_time = std::chrono::steady_clock::now();
    boost::asio::post(_raft_thread_pool, [this, rpc_data, raft_post_time]() {
        // Track Raft pool metrics
        auto raft_wait_time_us = get_elapsed_time_us(raft_post_time);
        auto raft_guard = atomic_counter_guard(_raft_pool_active_threads);

        auto gid = boost::uuids::string_generator()(rpc_data->request().group_id());
        auto& request = rpc_data->request();
        auto const& group_id = request.group_id();
        auto const& base = request.msg().base();

        // Lookup server
        auto it = _raft_servers.find(gid);
        if (it == _raft_servers.end()) {
            LOGD("Missing [group={}]", group_id);
            rpc_data->set_status(::grpc::Status(::grpc::NOT_FOUND,
                fmt::format("Missing RAFT group {}", group_id)));
            rpc_data->send_response();
            return;
        }

        // Record Raft pool wait time and active threads in service-level metrics
        HISTOGRAM_OBSERVE(_service_metrics, raft_pool_wait_time_us, raft_wait_time_us);
        GAUGE_UPDATE(_service_metrics, raft_pool_active_threads, _raft_pool_active_threads.load());

        // Record per-group metrics
        if (it->second.m_metrics) {
            COUNTER_INCREMENT(*it->second.m_metrics, group_steps, 1);
        }

        // Route based on message type
        auto msg_type = static_cast< nuraft::msg_type >(base.type());
        auto raft_server = it->second.m_server->raft_server();
        auto metrics = it->second.m_metrics;
        if (is_slow_message(msg_type)) {
            // SLOW PATH: Post to I/O pool
            COUNTER_INCREMENT(_service_metrics, io_pool_msg_count, 1);
            auto io_post_time = std::chrono::steady_clock::now();
            boost::asio::post(_io_thread_pool, [this, raft_server, metrics,
                                                  rpc_data, io_post_time, group_id, msg_type]() {
                auto io_wait_time_us = get_elapsed_time_us(io_post_time);
                auto io_guard = atomic_counter_guard(_io_pool_active_threads);

                auto exec_time_us = execute_step(raft_server, rpc_data, metrics);

                // Record I/O pool metrics in service-level metrics
                HISTOGRAM_OBSERVE(_service_metrics, io_pool_wait_time_us, io_wait_time_us);
                GAUGE_UPDATE(_service_metrics, io_pool_active_threads, _io_pool_active_threads.load());

                LOGT("I/O pool executed [group={}] [type={}] wait={}us exec={}us",
                     group_id, nuraft::msg_type_to_string(msg_type), io_wait_time_us, exec_time_us);
            });
        } else {
            // FAST PATH: Process on Raft thread
            COUNTER_INCREMENT(_service_metrics, raft_pool_msg_count, 1);
            execute_step(raft_server, rpc_data, metrics);
        }
    });
    return false;
}

std::shared_ptr< msg_service > msg_service::create(std::shared_ptr< ManagerImpl > const& manager,
                                                   group_id_t const& service_address,
                                                   std::string const& default_group_type,
                                                   bool const enable_data_service) {
    return std::make_shared< proto_service >(manager, service_address, default_group_type, enable_data_service);
}

} // namespace nuraft_mesg
