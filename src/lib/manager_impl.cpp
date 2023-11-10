/// Copyright 2018 (c) eBay Corporation
//
#include "manager_impl.hpp"

#include <chrono>

#include <boost/uuid/string_generator.hpp>
#include <ios>
#include <spdlog/fmt/ostr.h>
#include <spdlog/details/registry.h>

#include <libnuraft/async.hxx>
#include <sisl/options/options.h>
#include <sisl/grpc/rpc_server.hpp>
#include <sisl/grpc/generic_service.hpp>

#include "nuraft_mesg/mesg_factory.hpp"
#include "nuraft_mesg/nuraft_mesg.hpp"
#include "nuraft_mesg/grpc_server.hpp"
#include "proto/messaging_service.grpc.pb.h"
#include "logger.hpp"
#include "utils.hpp"

SISL_OPTION_GROUP(nuraft_mesg,
                  (messaging_metrics, "", "msg_metrics", "Gather metrics from SD Messaging", cxxopts::value< bool >(),
                   ""))

constexpr auto leader_change_timeout = std::chrono::milliseconds(3200);
constexpr auto grpc_client_threads = 1u;
constexpr auto grpc_server_threads = 1u;

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
    } catch (std::runtime_error & rte) { LOGE("Caught exception: [group={}] {}", group_id, rte.what()); }              \
    return folly::makeUnexpected(nuraft::CANCELLED);

namespace nuraft_mesg {

using lock_type = folly::SharedMutex;

int32_t to_server_id(peer_id_t const& server_addr) {
    boost::hash< boost::uuids::uuid > uuid_hasher;
    return uuid_hasher(server_addr) >> 33;
}

grpc_server_wrapper::grpc_server_wrapper(group_id_t const& group_id) {
    if (0 < SISL_OPTIONS.count("msg_metrics")) m_metrics = std::make_shared< group_metrics >(group_id);
}

class null_service final : public grpc_server {
public:
    using grpc_server::grpc_server;
    void associate(sisl::GrpcServer*) override{};
    void bind(sisl::GrpcServer*) override{};
};

class msg_group_listner : public nuraft::rpc_listener {
    std::weak_ptr< ManagerImpl > _mgr;
    group_id_t _group;

public:
    msg_group_listner(std::weak_ptr< ManagerImpl > mgr, group_id_t const& group) : _mgr(mgr), _group(group) {}
    ~msg_group_listner() {
        if (auto mgr = _mgr.lock(); mgr) mgr->shutdown_for(_group);
    }

    void listen(nuraft::ptr< nuraft::msg_handler >&) override { LOGI("[group={}]", _group); }
    void stop() override { LOGI("[group={}]", _group); }
    void shutdown() override { LOGI("[group={}]", _group); }
};

class engine_factory : public group_factory {
public:
    std::weak_ptr< MessagingApplication > application_;

    engine_factory(int const threads, group_id_t const& name,
                   std::shared_ptr< sisl::GrpcTokenClient > const token_client, std::string const& ssl_cert,
                   std::weak_ptr< MessagingApplication > app) :
            group_factory::group_factory(threads, name, token_client, ssl_cert), application_(app) {}

    std::string lookupEndpoint(peer_id_t const& client) override {
        LOGT("[peer={}]", client);
        if (auto a = application_.lock(); a) return a->lookup_peer(client);
        return std::string();
    }
};

ManagerImpl::~ManagerImpl() {
    LOGI("[srv_id={}] shutdown started.", start_params_.server_uuid_);
    if (_grpc_server) { _grpc_server->shutdown(); }
    std::deque< std::shared_ptr< grpc_server > > servers;
    {
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        for (auto& [k, v] : _raft_servers) {
            servers.push_back(v.m_server);
        }
    }

    for (auto& server : servers) {
        server->raft_server()->stop_server();
        server->raft_server()->shutdown();
    }
    LOGI("[srv_id={}] shutdown complete.", start_params_.server_uuid_);
}

void ManagerImpl::shutdown_for(group_id_t const& group_id) {
    {
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        LOGD("Shutting down [group={}]", group_id);
        if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
            _raft_servers.erase(it);
        } else {
            LOGW("Unknown [group={}] cannot shutdown.", group_id);
            return;
        }
    }
}

ManagerImpl::ManagerImpl(Manager::Params const& start_params, std::weak_ptr< MessagingApplication > app,
                         bool and_data_svc) :
        start_params_(start_params),
        _srv_id(to_server_id(start_params_.server_uuid_)),
        application_(app),
        _data_service_enabled(and_data_svc) {
    _g_factory = std::make_shared< engine_factory >(grpc_client_threads, start_params.server_uuid_,
                                                    start_params.token_client_, start_params.ssl_cert_, app);
    auto logger_name = fmt::format("nuraft_{}", start_params_.server_uuid_);
    //
    // NOTE: The Unit tests require this instance to be recreated with the same parameters.
    // This exception is only expected in this case where we "restart" the server by just recreating the instance.
    try {
        _custom_logger = sisl::logging::CreateCustomLogger(logger_name, "", false, false /* tee_to_stdout_stderr */);
    } catch (spdlog::spdlog_ex const& e) { _custom_logger = spdlog::details::registry::instance().get(logger_name); }

    sisl::logging::SetLogPattern("[%D %T.%f] [%^%L%$] [%t] %v", _custom_logger);
    nuraft::ptr< nuraft::logger > logger =
        std::make_shared< nuraft_mesg_logger >(start_params_.server_uuid_, _custom_logger);

    // RAFT request scheduler
    nuraft::asio_service::options service_options;
    service_options.thread_pool_size_ = 1;
    _scheduler = std::make_shared< nuraft::asio_service >(service_options, logger);

    // Start a gRPC server and create and associate nuraft_mesg services.
    restart_server();
}

void ManagerImpl::restart_server() {
    auto listen_address = fmt::format(FMT_STRING("0.0.0.0:{}"), start_params_.mesg_port_);
    LOGI("Starting Messaging Service on http://{}", listen_address);

    std::lock_guard< std::mutex > lg(_manager_lock);
    _grpc_server.reset();
    _grpc_server = std::unique_ptr< sisl::GrpcServer >(
        sisl::GrpcServer::make(listen_address, start_params_.token_verifier_, grpc_server_threads,
                               start_params_.ssl_key_, start_params_.ssl_cert_));

    if (!_grpc_server->register_async_service< Messaging >()) {
        RELEASE_ASSERT(false, "Could not register RaftSvc with gRPC!");
    }
    if (_data_service_enabled) {
        _data_service.set_grpc_server(_grpc_server.get());
        _data_service.associate();
    }

    _grpc_server->run();

    if (!_grpc_server->register_rpc< Messaging, RaftGroupMsg, RaftGroupMsg, false >(
            "RaftStep", &Messaging::AsyncService::RequestRaftStep,
            std::bind(&ManagerImpl::raftStep, this, std::placeholders::_1))) {
        RELEASE_ASSERT(false, "Could not bind gRPC ::RaftStep to routine!");
    }
    if (_data_service_enabled) { _data_service.bind(); }
}

bool ManagerImpl::raftStep(const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data) {
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
    if (sid != start_params_.server_uuid_) {
        LOGW("Recieved mesg for {} intended for {}, we are {}",
             nuraft::msg_type_to_string(nuraft::msg_type(base.type())), intended_addr, start_params_.server_uuid_);
        rpc_data->set_status(::grpc::Status(
            ::grpc::INVALID_ARGUMENT,
            fmt::format(FMT_STRING("intended addr: [{}], our addr: [{}]"), intended_addr, start_params_.server_uuid_)));
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

void ManagerImpl::register_mgr_type(group_type_t const& group_type, group_params const& params) {
    std::lock_guard< std::mutex > lg(_manager_lock);
    auto [it, happened] = _state_mgr_types.emplace(std::make_pair(group_type, params));
    DEBUG_ASSERT(_state_mgr_types.end() != it, "Out of memory?");
    DEBUG_ASSERT(!!happened, "Re-register?");
    if (_state_mgr_types.end() == it) { LOGE("Could not register [group_type={}]", group_type); }
}

nuraft::cb_func::ReturnCode ManagerImpl::raft_event(group_id_t const& group_id, nuraft::cb_func::Type type,
                                                    nuraft::cb_func::Param* param) {
    switch (type) {
    case nuraft::cb_func::RemovedFromCluster: {
        LOGI("[srv_id={}] evicted from: [group={}]", start_params_.server_uuid_, group_id);
        exit_group(group_id);
    } break;
    case nuraft::cb_func::JoinedCluster: {
        auto const my_id = param->myId;
        auto const leader_id = param->leaderId;
        LOGI("[srv_id={}] joined: [group={}], [leader_id:{},my_id:{}]", start_params_.server_uuid_, group_id, leader_id,
             my_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = (leader_id == my_id);
        }
    } break;
    case nuraft::cb_func::NewConfig: {
        LOGD("[srv_id={}] saw cluster change: [group={}]", start_params_.server_uuid_, group_id);
        _config_change.notify_all();
    } break;
    case nuraft::cb_func::BecomeLeader: {
        LOGI("[srv_id={}] became leader: [group={}]!", start_params_.server_uuid_, group_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = true;
        }
        _config_change.notify_all();
    } break;
    case nuraft::cb_func::BecomeFollower: {
        LOGI("[srv_id={}] following: [group={}]!", start_params_.server_uuid_, group_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = false;
        }
    }
    default:
        break;
    };
    return nuraft::cb_func::Ok;
}

void ManagerImpl::exit_group(group_id_t const& group_id) {
    std::shared_ptr< mesg_state_mgr > mgr;
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (auto it = _state_managers.find(group_id); it != _state_managers.end()) { mgr = it->second; }
    }
    if (mgr) mgr->leave();
}

nuraft::cmd_result_code ManagerImpl::group_init(int32_t const srv_id, group_id_t const& group_id,
                                                group_type_t const& group_type, nuraft::context*& ctx,
                                                std::shared_ptr< nuraft_mesg::group_metrics > metrics) {
    LOGD("Creating context for: [group_id={}] as Member: {}", group_id, srv_id);

    // State manager (RAFT log store, config)
    std::shared_ptr< nuraft::state_mgr > smgr;
    std::shared_ptr< nuraft::state_machine > sm;
    nuraft::raft_params params;
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        auto def_group = _state_mgr_types.end();
        if (def_group = _state_mgr_types.find(group_type); _state_mgr_types.end() == def_group) {
            return nuraft::cmd_result_code::SERVER_NOT_FOUND;
        }
        params = def_group->second;

        auto [it, happened] = _state_managers.emplace(group_id, nullptr);
        if (it != _state_managers.end()) {
            if (happened) {
                // A new logstore!
                LOGD("Creating new State Manager for: [group={}], type: {}", group_id, group_type);
                it->second = application_.lock()->create_state_mgr(srv_id, group_id);
            }
            it->second->become_ready();
            sm = it->second->get_state_machine();
            smgr = it->second;
        } else {
            return nuraft::cmd_result_code::CANCELLED;
        }
    }

    // RAFT client factory
    std::shared_ptr< nuraft::rpc_client_factory > rpc_cli_factory(
        std::make_shared< nuraft_mesg::mesg_factory >(_g_factory, group_id, group_type, metrics));

    // RAFT service interface (stops gRPC service etc...) (TODO)
    std::shared_ptr< nuraft::rpc_listener > listener;

    nuraft::ptr< nuraft::logger > logger = std::make_shared< nuraft_mesg_logger >(group_id, _custom_logger);
    ctx = new nuraft::context(smgr, sm, listener, logger, rpc_cli_factory, _scheduler, params);
    ctx->set_cb_func([this, group_id](nuraft::cb_func::Type type, nuraft::cb_func::Param* param) mutable {
        return this->raft_event(group_id, type, param);
    });

    return nuraft::cmd_result_code::OK;
}

NullAsyncResult ManagerImpl::add_member(group_id_t const& group_id, peer_id_t const& new_id) {
    auto str_id = to_string(new_id);
    std::shared_ptr< grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) { server = it->second.m_server; }
    }
    if (!server) return folly::makeUnexpected(nuraft::SERVER_NOT_FOUND);
    return
        [&]() -> NullAsyncResult { CONTINUE_RESP(server->add_srv(nuraft::srv_config(to_server_id(new_id), str_id))) }()
                     .deferValue([this, g_id = group_id,
                                  n_id = std::move(str_id)](auto cmd_result) mutable -> NullResult {
                         if (!cmd_result) return folly::makeUnexpected(cmd_result.error());
                         // TODO This should not block, but attach a new promise!
                         auto lk = std::unique_lock< std::mutex >(_manager_lock);
                         if (!_config_change.wait_for(
                                 lk, leader_change_timeout, [this, g_id = std::move(g_id), n_id = std::move(n_id)]() {
                                     std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
                                     get_srv_config_all(g_id, srv_list);
                                     return std::find_if(srv_list.begin(), srv_list.end(),
                                                         [n_id = std::move(n_id)](
                                                             const std::shared_ptr< nuraft::srv_config >& cfg) {
                                                             return n_id == cfg->get_endpoint();
                                                         }) != srv_list.end();
                                 })) {
                             return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
                         }
                         return folly::Unit();
                     });
}

NullAsyncResult ManagerImpl::rem_member(group_id_t const& group_id, peer_id_t const& old_id) {
    std::shared_ptr< grpc_server > server;
    {
        auto rl = std::shared_lock< lock_type >(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) { server = it->second.m_server; }
    }
    if (server) { CONTINUE_RESP(server->rem_srv(to_server_id(old_id))) }
    return folly::makeUnexpected(nuraft::SERVER_NOT_FOUND);
}

NullAsyncResult ManagerImpl::become_leader(group_id_t const& group_id) {
    {
        auto lk = std::unique_lock< std::mutex >(_manager_lock);
        if (_is_leader[group_id]) { return folly::Unit(); }
    }

    return folly::makeSemiFuture< folly::Unit >(folly::Unit())
        .deferValue([this, g_id = group_id](auto) mutable -> NullResult {
            std::shared_ptr< grpc_server > server;
            {
                std::shared_lock< lock_type > rl(_raft_servers_lock);
                if (auto it = _raft_servers.find(g_id); _raft_servers.end() != it) { server = it->second.m_server; }
            }
            if (!server) return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
            try {
                server->request_leadership();
            } catch (std::runtime_error& rte) {
                LOGW("Caught exception during request_leadership(): {}", rte.what())
                return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
            }
            auto lk = std::unique_lock< std::mutex >(_manager_lock);
            if (!_config_change.wait_for(lk, leader_change_timeout,
                                         [this, g_id = std::move(g_id)]() { return _is_leader[g_id]; }))
                return folly::makeUnexpected(nuraft::cmd_result_code::TIMEOUT);
            return folly::Unit();
        });
}

NullAsyncResult ManagerImpl::append_entries(group_id_t const& group_id,
                                            std::vector< std::shared_ptr< nuraft::buffer > > const& logs) {
    std::shared_ptr< grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) { server = it->second.m_server; }
    }
    if (server) { CONTINUE_RESP(server->append_entries(logs)) }
    return folly::makeUnexpected(nuraft::SERVER_NOT_FOUND);
}

std::shared_ptr< mesg_state_mgr > ManagerImpl::lookup_state_manager(group_id_t const& group_id) const {
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) return it->second;
    return nullptr;
}

nuraft::cmd_result_code ManagerImpl::joinRaftGroup(int32_t const srv_id, group_id_t const& group_id,
                                                   group_type_t const& group_type) {
    LOGI("[srv_id={}] joining [group={}, type={}]", start_params_.server_uuid_, group_id, group_type);

    nuraft::context* ctx{nullptr};
    bool happened{false};
    auto it = _raft_servers.end();
    {
        // This is for backwards compatibility for groups that had no type before.
        auto g_type = group_type;
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        if (g_type.empty()) { g_type = start_params_.default_group_type_; }
        std::tie(it, happened) = _raft_servers.emplace(std::make_pair(group_id, group_id));
        if (_raft_servers.end() != it && happened) {
            if (auto err = group_init(srv_id, group_id, g_type, ctx, it->second.m_metrics); err) {
                LOGE("Error during RAFT server creation [group={}]: {}", group_id, err);
                return err;
            }
            DEBUG_ASSERT(!ctx->rpc_listener_, "RPC listner should not be set!");
            auto new_listner = std::make_shared< msg_group_listner >(shared_from_this(), group_id);
            ctx->rpc_listener_ = std::static_pointer_cast< nuraft::rpc_listener >(new_listner);
            auto server = std::make_shared< nuraft::raft_server >(ctx);
            it->second.m_server = std::make_shared< null_service >(server);
            if (_data_service_enabled) {
                auto smgr = std::dynamic_pointer_cast< mesg_state_mgr >(ctx->state_mgr_);
                auto cli_factory = std::dynamic_pointer_cast< mesg_factory >(ctx->rpc_cli_factory_);
                smgr->make_repl_ctx(it->second.m_server.get(), cli_factory);
            }
        }
    }
    return nuraft::cmd_result_code::OK;
}

NullAsyncResult ManagerImpl::create_group(group_id_t const& group_id, std::string const& group_type_name) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _is_leader.insert(std::make_pair(group_id, false));
    }
    if (auto const err = joinRaftGroup(_srv_id, group_id, group_type_name); err) return folly::makeUnexpected(err);

    // Wait for the leader election timeout to make us the leader
    return folly::makeSemiFuture< folly::Unit >(folly::Unit())
        .deferValue([this, g_id = group_id](auto) mutable -> NullResult {
            auto lk = std::unique_lock< std::mutex >(_manager_lock);
            if (!_config_change.wait_for(lk, leader_change_timeout,
                                         [this, g_id = std::move(g_id)]() { return _is_leader[g_id]; })) {
                return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
            }
            return folly::Unit();
        });
}

NullResult ManagerImpl::join_group(group_id_t const& group_id, group_type_t const& group_type,
                                   std::shared_ptr< mesg_state_mgr > smgr) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        auto [it, happened] = _state_managers.emplace(group_id, smgr);
        if (_state_managers.end() == it) return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
    }
    if (auto const err = joinRaftGroup(_srv_id, group_id, group_type); err) {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _state_managers.erase(group_id);
        return folly::makeUnexpected(err);
    }
    return folly::Unit();
}

void ManagerImpl::append_peers(group_id_t const& group_id, std::list< peer_id_t >& servers) const {
    auto it = _state_managers.end();
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (it = _state_managers.find(group_id); _state_managers.end() == it) return;
    }
    if (auto config = it->second->load_config(); config) {
        for (auto const& server : config->get_servers()) {
            servers.push_back(boost::uuids::string_generator()(server->get_endpoint()));
        }
    }
}

void ManagerImpl::leave_group(group_id_t const& group_id) {
    LOGI("Leaving group [group={}]", group_id);
    std::shared_ptr< grpc_server > server;
    {
        std::unique_lock< lock_type > lck(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
            server = it->second.m_server;
        } else {
            LOGW("Unknown [group={}] cannot part.", group_id);
        }
    }

    if (server) {
        if (auto raft_server = server->raft_server(); raft_server) {
            LOGI("[group={}]", group_id);
            raft_server->stop_server();
            raft_server->shutdown();
        }
    }

    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) {
        // Delete all the state files (RAFT log etc.) after descrtuctor is called.
        it->second->permanent_destroy();
        _state_managers.erase(it);
    } else
        LOGW("Unknown [state_mgr={}] cannot destroy.", group_id);

    LOGI("Finished leaving: [group={}]", group_id);
}

uint32_t ManagerImpl::logstore_id(group_id_t const& group_id) const {
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) { return it->second->get_logstore_id(); }
    return UINT32_MAX;
}

void ManagerImpl::get_srv_config_all(group_id_t const& group_id,
                                     std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out) {
    std::shared_ptr< grpc_server > server;
    {
        std::shared_lock< lock_type > rl(_raft_servers_lock);
        if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) { server = it->second.m_server; }
    }
    if (server) {
        try {
            server->get_srv_config_all(configs_out);
        } catch (std::runtime_error& rte) { LOGE("Caught exception during add_srv(): {}", rte.what()); }
    }
}

bool ManagerImpl::bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                            data_service_request_handler_t const& request_handler) {
    if (!_data_service_enabled) {
        LOGE("Could not register data service method {}; data service is null", request_name);
        return false;
    }
    return _data_service.bind(request_name, group_id, request_handler);
}

// The endpoint field of the raft_server config is the uuid of the server.
const std::string repl_service_ctx_grpc::id_to_str(int32_t const id) const {
    auto const& srv_config = _server->get_config()->get_server(id);
    return (srv_config) ? srv_config->get_endpoint() : std::string();
}

const std::optional< Result< peer_id_t > > repl_service_ctx_grpc::get_peer_id(destination_t const& dest) const {
    if (std::holds_alternative< peer_id_t >(dest)) return std::get< peer_id_t >(dest);
    if (std::holds_alternative< role_regex >(dest)) {
        if (!_server) {
            LOGW("server not initialized");
            return folly::makeUnexpected(nuraft::cmd_result_code::SERVER_NOT_FOUND);
        }
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

void mesg_state_mgr::make_repl_ctx(grpc_server* server, std::shared_ptr< mesg_factory > const& cli_factory) {
    m_repl_svc_ctx = std::make_unique< repl_service_ctx_grpc >(server, cli_factory);
}

std::shared_ptr< Manager > init_messaging(Manager::Params const& p, std::weak_ptr< MessagingApplication > w,
                                          bool with_data_svc) {
    RELEASE_ASSERT(w.lock(), "Could not acquire application!");
    return std::make_shared< ManagerImpl >(p, w, with_data_svc);
}

} // namespace nuraft_mesg
