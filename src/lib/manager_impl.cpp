/// Copyright 2018 (c) eBay Corporation
//
#include "manager_impl.hpp"

#include <chrono>
#include <future>
#include <thread>

#include <boost/uuid/string_generator.hpp>
#include <ios>
#include <spdlog/fmt/ostr.h>
#include <spdlog/details/registry.h>

#include <libnuraft/async.hxx>
#include <algorithm>
#include <ranges>

#include <stdexec/execution.hpp>
#include <exec/timed_scheduler.hpp>

#include <sisl/options/options.h>
#include <sisl/grpc/rpc_server.hpp>
#include <sisl/grpc/generic_service.hpp>

#include "lib/mesg_factory.hpp"
#include "nuraft_mesg/mesg_state_mgr.hpp"
#include "nuraft_mesg/nuraft_mesg.hpp"

#include "repl_service_ctx.hpp"
#include "service.hpp"
#include "logger.hpp"
#include "nuraft_mesg_config.hpp"

SISL_LOGGING_DEF(nuraft_mesg)

namespace nuraft_mesg {

int32_t to_server_id(peer_id_t const& server_addr) {
    boost::hash< boost::uuids::uuid > uuid_hasher;
    return uuid_hasher(server_addr) >> 33;
}

messaging_application::messaging_application() {
    sisl::VersionMgr::addVersion(PACKAGE_NAME, version::Semver200_version(PACKAGE_VERSION));
}

class engine_factory : public group_factory {
public:
    std::weak_ptr< messaging_application > application_;

    engine_factory(int const raft_threads, int const data_threads, manager::params const& start_params,
                   std::weak_ptr< messaging_application > app) :
            group_factory::group_factory(raft_threads, data_threads, start_params.server_uuid_,
                                         start_params.token_client_, start_params.ssl_ca_,
                                         start_params.max_receive_message_size_, start_params.max_send_message_size_),
            application_(app) {}

    std::string lookup_endpoint(peer_id_t const& client) override {
        LOGT("[peer={}]", client);
        if (auto a = application_.lock(); a) return a->lookup_peer(client);
        return std::string();
    }
};

ManagerImpl::~ManagerImpl() {
    if (_mesg_service) {
        // IMPORTANT: The order matters. nuraft can be using the grpc server that might crash the system if grpc server
        // is shutdown first.
        _mesg_service->shutdown();
        _grpc_server->shutdown();
    }
}

ManagerImpl::ManagerImpl(manager::params const& start_params, std::weak_ptr< messaging_application > app) :
        start_params_(start_params), _srv_id(to_server_id(start_params_.server_uuid_)), application_(app) {
    _g_factory =
        std::make_shared< engine_factory >(NURAFT_MESG_CONFIG(grpc_raft_client_thread_cnt),
                                           NURAFT_MESG_CONFIG(grpc_data_client_thread_cnt), start_params_, app);
    auto logger_name = fmt::format("nuraft_{}", start_params_.server_uuid_);
    //
    // NOTE: The Unit tests require this instance to be recreated with the same parameters.
    // This exception is only expected in this case where we "restart" the server by just recreating the instance.
    try {
        _custom_logger =
            sisl::logging::CreateCustomLogger(logger_name, "", start_params_.enable_console_log_,
                                              start_params_.enable_console_log_ /* tee_to_stdout_stderr */);
    } catch (spdlog::spdlog_ex const& e) {
        _custom_logger = spdlog::details::registry::instance().get(logger_name);
    }

    sisl::logging::SetLogPattern("[%D %T.%f] [%^%L%$] [%t] %v", _custom_logger);
    nuraft::ptr< nuraft::logger > logger =
        std::make_shared< nuraft_mesg_logger >(start_params_.server_uuid_, _custom_logger);

    // RAFT request scheduler
    nuraft::asio_service::options service_options;
    service_options.thread_pool_size_ = NURAFT_MESG_CONFIG(raft_scheduler_thread_cnt);
    _scheduler = std::make_shared< nuraft::asio_service >(service_options, logger);
}

void ManagerImpl::start(bool and_data_svc) {
    if (auto lg = std::lock_guard< std::mutex >(_manager_lock); !_mesg_service) {
        _mesg_service = msg_service::create(shared_from_this(), start_params_.server_uuid_,
                                            start_params_.default_group_type_, and_data_svc);
    }
    restart_server();
}

void ManagerImpl::restart_server() {
    auto listen_address = fmt::format(FMT_STRING("0.0.0.0:{}"), start_params_.mesg_port_);
    LOGI("Starting Messaging Service on http://{}", listen_address);

    std::lock_guard< std::mutex > lg(_manager_lock);
    RELEASE_ASSERT(_mesg_service, "Need to call ::start() first!");
    sisl::GrpcServer* tmp_server = nullptr;
    try {
        tmp_server = sisl::GrpcServer::make(listen_address, start_params_.token_verifier_,
                                            NURAFT_MESG_CONFIG(grpc_server_thread_cnt), start_params_.ssl_key_,
                                            start_params_.ssl_cert_, start_params_.max_receive_message_size_,
                                            start_params_.max_send_message_size_);
    } catch (std::runtime_error const& e) {
        LOGERROR("Failed to create GRPC server for Messaging Service: {}", e.what());
        return;
    }
    if (!tmp_server) {
        LOGERROR("Failed to create GRPC server: for Messaging Service");
        return;
    }

    _grpc_server.reset();
    _grpc_server = std::unique_ptr< sisl::GrpcServer >(tmp_server);
    _mesg_service->associate(_grpc_server.get());

    _grpc_server->run();
    _mesg_service->bind(_grpc_server.get());
}

void ManagerImpl::register_mgr_type(group_type_t const& group_type, group_params const& params) {
    std::lock_guard< std::mutex > lg(_manager_lock);
    auto [it, happened] = _state_mgr_types.emplace(std::make_pair(group_type, params));
    DEBUG_ASSERT(_state_mgr_types.end() != it, "Out of memory?");
    DEBUG_ASSERT(!!happened, "Re-register?");
    if (_state_mgr_types.end() == it) {
        LOGE("Could not register [group_type={}]", group_type);
    }
}

void ManagerImpl::generic_raft_event_handler(group_id_t const& group_id, nuraft::cb_func::Type type,
                                             nuraft::cb_func::Param* param) {
    auto const& my_id = param->myId;
    auto const& leader_id = param->leaderId;
    switch (type) {
    case nuraft::cb_func::RemovedFromCluster: {
        LOGI("[srv_id={}] evicted from: [group={}, leader_id:{}, my_id:{}]", start_params_.server_uuid_, group_id,
             leader_id, my_id);
        exit_group(group_id);
    } break;
    case nuraft::cb_func::JoinedCluster: {
        LOGI("[srv_id={}] joined: [group={}, leader_id:{}, my_id:{}]", start_params_.server_uuid_, group_id, leader_id,
             my_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = (leader_id == my_id);
        }
    } break;
    case nuraft::cb_func::NewConfig: {
        LOGD("[srv_id={}] saw cluster change: [group={}, leader_id:{}, my_id:{}]", start_params_.server_uuid_, group_id,
             leader_id, my_id);
        signal_waiters(group_id);
    } break;
    case nuraft::cb_func::BecomeLeader: {
        LOGI("[srv_id={}] became leader: [group={}, leader_id:{}, my_id:{}]!", start_params_.server_uuid_, group_id,
             leader_id, my_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = true;
        }
        signal_waiters(group_id);
    } break;
    case nuraft::cb_func::BecomeFollower: {
        LOGI("[srv_id={}] following: [group={}, leader_id:{}, my_id:{}]!", start_params_.server_uuid_, group_id,
             leader_id, my_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = false;
        }
    } break;
    case nuraft::cb_func::SaveSnapshot: {
        LOGI("Received Snapshot to sync for: {}, [leader_id:{}, my_id:{}]", group_id, leader_id, my_id);
    } break;
    case nuraft::cb_func::FollowerLost: {
        LOGI("Lost follower: {}, [leader_id:{}, my_id:{}]", param->peerId, group_id, leader_id, my_id);
    } break;
    default:
        break;
    };
}

void ManagerImpl::exit_group(group_id_t const& group_id) {
    std::shared_ptr< mesg_state_mgr > mgr;
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (auto it = _state_managers.find(group_id); it != _state_managers.end()) {
            mgr = it->second;
        }
    }
    if (mgr) mgr->leave();
}

nuraft::cmd_result_code ManagerImpl::group_init(int32_t const srv_id, group_id_t const& group_id,
                                                group_type_t const& group_type, nuraft::context*& ctx,
                                                std::shared_ptr< nuraft_mesg::group_metrics > metrics) {
    LOGD("Creating context for: [group_id={}] as Member: {}", group_id, srv_id);

    // State manager (RAFT log store, config)
    std::shared_ptr< mesg_state_mgr > smgr;
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
                LOGD("Creating new State manager for: [group={}], type: {}", group_id, group_type);
                it->second = application_.lock()->create_state_mgr(srv_id, group_id);
            }
            smgr = it->second;
            smgr->become_ready();
            sm = smgr->get_state_machine();
            smgr->set_manager_impl(shared_from_this());
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
    auto base_smgr = std::static_pointer_cast< nuraft::state_mgr >(smgr);
    ctx = new nuraft::context(base_smgr, sm, listener, logger, rpc_cli_factory, _scheduler, params);
    ctx->set_cb_func([wp = std::weak_ptr< mesg_state_mgr >(smgr), group_id](nuraft::cb_func::Type type,
                                                                            nuraft::cb_func::Param* param) {
        if (auto sp = wp.lock(); sp) {
            return sp->internal_raft_event_handler(group_id, type, param);
        }
        return nuraft::cb_func::Ok;
    });

    return nuraft::cmd_result_code::OK;
}

void ManagerImpl::signal_waiters(group_id_t const& group_id) {
    std::vector< std::shared_ptr< wakeup_event > > to_signal;
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (auto it = _waiters.find(group_id); it != _waiters.end()) { to_signal = it->second; }
    }
    // Signal outside the lock: signal() may resume the waiting coroutine, which re-acquires _manager_lock to
    // re-check its predicate.
    for (auto& ev : to_signal) { ev->signal(true); }
}

null_async_task ManagerImpl::wait_for_condition(group_id_t group_id, std::function< bool() > pred,
                                              std::chrono::steady_clock::time_point deadline,
                                              nuraft::cmd_result_code timeout_code) {
    auto sched = _timer_ctx.get_scheduler();
    for (;;) {
        // Run the waiter bookkeeping and pred() on the timer thread, never inline on whoever signalled us.
        // signal_waiters() fires from inside a nuraft raft_server callback while nuraft holds the raft_server
        // lock; if value_awaitable::complete() resumed this coroutine inline there, pred() ->
        // get_srv_config_all() would take _raft_servers_mutex under the raft_server lock -- inverting the order
        // every msg_service method uses (_raft_servers_mutex first, then the raft_server lock) and risking a
        // deadlock. Hopping to the timer thread first keeps pred()'s locks off the nuraft callback thread.
        co_await exec::schedule_after(sched, std::chrono::milliseconds(0));
        // Register the wakeup BEFORE checking pred so a config change cannot slip between the check and the
        // wait: signal_waiters and the _is_leader writes share _manager_lock, so once we are registered any
        // later change either updates state our pred() then observes, or signals this event.
        auto ev = std::make_shared< wakeup_event >();
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _waiters[group_id].push_back(ev);
        }
        bool const satisfied = pred();
        bool const expired = std::chrono::steady_clock::now() >= deadline;
        if (satisfied || expired) {
            std::lock_guard< std::mutex > lg(_manager_lock);
            std::erase(_waiters[group_id], ev);
            if (satisfied) co_return null_result{};
            co_return std::unexpected(to_condition(timeout_code));
        }
        // Arm the deadline timer to also wake us (first-wins with a real config-change signal).
        stdexec::start_detached(exec::schedule_at(sched, deadline) |
                                stdexec::then([ev]() noexcept { ev->signal(false); }));
        co_await ev->_av;
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            std::erase(_waiters[group_id], ev);
        }
    }
}

null_async_task ManagerImpl::add_member(group_id_t const& group_id, peer_id_t const& new_id) {
    auto str_id = to_string(new_id);
    auto srv_config = nuraft::srv_config(to_server_id(new_id), str_id);
    return add_member(group_id, srv_config);
}

sisl::async::task< nuraft::cmd_result_code >
ManagerImpl::retry_config_changing(std::function< sisl::async::task< nuraft::cmd_result_code >() > dispatch,
                                   std::chrono::steady_clock::time_point deadline) {
    auto sched = _timer_ctx.get_scheduler();
    for (;;) {
        auto const code = co_await dispatch();
        if ((code == nuraft::CONFIG_CHANGING || code == nuraft::SERVER_IS_JOINING) &&
            std::chrono::steady_clock::now() < deadline) {
            co_await exec::schedule_after(sched, std::chrono::milliseconds(500));
            continue;
        }
        co_return code;
    }
}

null_async_task ManagerImpl::add_member(group_id_t const& group_id, nuraft::srv_config const& srv_config) {
    // Clone srv_config eagerly (it is non-copyable and the coroutine below is lazy; a const& would dangle
    // once co_awaited later). serialize() runs now, while srv_config is alive.
    return add_member_impl(group_id, srv_config.serialize());
}

null_async_task ManagerImpl::add_member_impl(group_id_t group_id, nuraft::ptr< nuraft::buffer > cfg_buf) {
    cfg_buf->pos(0);
    auto const cfg = nuraft::srv_config::deserialize(*cfg_buf); // ptr<srv_config> owned by this frame
    auto const endpoint = cfg->get_endpoint();
    auto sched = _timer_ctx.get_scheduler();
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(NURAFT_MESG_CONFIG(raft_leader_change_timeout_ms));
    // Retry the local add_srv while the config is changing (this used to be the consumer's
    // retry_when_config_changing loop). ALREADY_EXISTS is idempotent success; any other non-OK code fails.
    nuraft::cmd_result_code code;
    for (;;) {
        code = co_await _mesg_service->add_member(group_id, *cfg);
        if ((code == nuraft::CONFIG_CHANGING || code == nuraft::SERVER_IS_JOINING) &&
            std::chrono::steady_clock::now() < deadline) {
            co_await exec::schedule_after(sched, std::chrono::milliseconds(500));
            continue;
        }
        break;
    }
    if (code != nuraft::OK && code != nuraft::SERVER_ALREADY_EXISTS) co_return std::unexpected(to_condition(code));
    // Confirm the new member appears in config. check_member reads raft state WITHOUT _manager_lock:
    // get_srv_config_all takes _raft_servers_mutex, and the nuraft callback path holds _raft_servers_mutex
    // then _manager_lock, so holding _manager_lock here would invert the order.
    co_return co_await wait_for_condition(
        group_id,
        [this, group_id, endpoint]() {
            std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
            _mesg_service->get_srv_config_all(group_id, srv_list);
            return std::ranges::any_of(srv_list, [&](auto const& cfg) { return endpoint == cfg->get_endpoint(); });
        },
        deadline, nuraft::cmd_result_code::CANCELLED);
}

null_async_task ManagerImpl::rem_member(group_id_t const& group_id, peer_id_t const& old_id) {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(NURAFT_MESG_CONFIG(raft_leader_change_timeout_ms));
    auto const member_id = to_server_id(old_id);
    auto const code = co_await retry_config_changing(
        [this, group_id, member_id]() { return _mesg_service->rem_member(group_id, member_id); }, deadline);
    // SERVER_NOT_FOUND is idempotent success (the member is already gone).
    if (code == nuraft::OK || code == nuraft::SERVER_NOT_FOUND) co_return null_result{};
    co_return std::unexpected(to_condition(code));
}

null_async_task ManagerImpl::become_leader(group_id_t const& group_id) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (_is_leader[group_id]) co_return null_result{};
    }
    if (!lookup_state_manager(group_id))
        co_return std::unexpected(to_condition(nuraft::cmd_result_code::SERVER_NOT_FOUND));

    auto sched = _timer_ctx.get_scheduler();
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(NURAFT_MESG_CONFIG(raft_leader_change_timeout_ms));
    // request_leadership() returns false when the group doesn't yet know its leader (e.g. just after a
    // restart). Retry with a short async delay until it is accepted or the deadline passes.
    while (!_mesg_service->become_leader(group_id)) {
        if (std::chrono::steady_clock::now() >= deadline)
            co_return std::unexpected(to_condition(nuraft::cmd_result_code::TIMEOUT));
        co_await exec::schedule_after(sched, std::chrono::milliseconds(50));
    }
    co_return co_await wait_for_condition(
        group_id, [this, group_id]() { std::lock_guard< std::mutex > lg(_manager_lock); return _is_leader[group_id]; },
        deadline, nuraft::cmd_result_code::TIMEOUT);
}

null_async_task ManagerImpl::append_entries(group_id_t const& group_id,
                                          std::vector< std::shared_ptr< nuraft::buffer > > const& buf) {
    co_return to_null_result(co_await _mesg_service->append_entries(group_id, buf));
}

std::shared_ptr< mesg_state_mgr > ManagerImpl::lookup_state_manager(group_id_t const& group_id) const {
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) return it->second;
    return nullptr;
}

null_async_task ManagerImpl::create_group(group_id_t const& group_id, std::string const& group_type_name) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _is_leader.insert(std::make_pair(group_id, false));
    }
    // joinRaftGroup is dispatched eagerly here (a dropped result still creates the group); the returned task
    // only carries the wait for this node to win the election.
    if (auto const err = _mesg_service->joinRaftGroup(_srv_id, group_id, group_type_name); err) {
        return make_ready< null_result >(std::unexpected(to_condition(err)));
    }
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(NURAFT_MESG_CONFIG(raft_leader_change_timeout_ms));
    return wait_for_condition(
        group_id, [this, group_id]() { std::lock_guard< std::mutex > lg(_manager_lock); return _is_leader[group_id]; },
        deadline, nuraft::cmd_result_code::CANCELLED);
}

null_result ManagerImpl::join_group(group_id_t const& group_id, group_type_t const& group_type,
                                   std::shared_ptr< mesg_state_mgr > smgr) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        auto [it, happened] = _state_managers.emplace(group_id, smgr);
        if (_state_managers.end() == it) return std::unexpected(to_condition(nuraft::cmd_result_code::CANCELLED));
    }
    if (auto const err = _mesg_service->joinRaftGroup(_srv_id, group_id, group_type); err) {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _state_managers.erase(group_id);
        return std::unexpected(to_condition(err));
    }
    return {};
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
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (0 == _state_managers.count(group_id)) {
            LOGD("Asked to leave [group={}] which we are not part of!", group_id);
            return;
        }
    }

    _mesg_service->leave_group(group_id);

    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) {
        // Delete all the state files (RAFT log etc.) after descrtuctor is called.
        it->second->permanent_destroy();
        _state_managers.erase(it);
    }

    LOGI("Finished leaving: [group={}]", group_id);
}

uint32_t ManagerImpl::logstore_id(group_id_t const& group_id) const {
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) {
        return it->second->get_logstore_id();
    }
    return UINT32_MAX;
}

void ManagerImpl::get_srv_config_all(group_id_t const& group_id,
                                     std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out) {
    _mesg_service->get_srv_config_all(group_id, configs_out);
}

bool ManagerImpl::bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                            data_service_request_handler_t const& request_handler) {
    RELEASE_ASSERT(_mesg_service, "Need to call ::start() first!");
    return _mesg_service->bind_data_service_request(request_name, group_id, request_handler);
}

void mesg_state_mgr::make_repl_ctx(grpc_server* server, std::shared_ptr< mesg_factory > const& cli_factory) {
    m_repl_svc_ctx = std::make_unique< repl_service_ctx_grpc >(server, cli_factory);
}

nuraft::cb_func::ReturnCode mesg_state_mgr::internal_raft_event_handler(group_id_t const& group_id,
                                                                        nuraft::cb_func::Type type,
                                                                        nuraft::cb_func::Param* param) {
    // Have we shutdown?
    if (auto sp = m_manager.lock(); sp)
        sp->generic_raft_event_handler(group_id, type, param);
    else
        return nuraft::cb_func::ReturnNull;
    return raft_event(type, param);
}

std::shared_ptr< manager > init_messaging(manager::params const& p, std::weak_ptr< messaging_application > w,
                                          bool with_data_svc) {
    RELEASE_ASSERT(w.lock(), "Could not acquire application!");
    auto m = std::make_shared< ManagerImpl >(p, w);
    m->start(with_data_svc);
    return m;
}

} // namespace nuraft_mesg
