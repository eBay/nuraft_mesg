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

class engine_factory : public group_factory {
public:
    std::weak_ptr< MessagingApplication > application_;

    engine_factory(int const raft_threads, int const data_threads, Manager::Params const& start_params,
                   std::weak_ptr< MessagingApplication > app) :
            group_factory::group_factory(raft_threads, data_threads, start_params.server_uuid_,
                                         start_params.token_client_, start_params.ssl_cert_),
            application_(app) {}

    std::string lookupEndpoint(peer_id_t const& client) override {
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

ManagerImpl::ManagerImpl(Manager::Params const& start_params, std::weak_ptr< MessagingApplication > app) :
        start_params_(start_params), _srv_id(to_server_id(start_params_.server_uuid_)), application_(app) {
    _g_factory =
        std::make_shared< engine_factory >(NURAFT_MESG_CONFIG(grpc_raft_client_thread_cnt),
                                           NURAFT_MESG_CONFIG(grpc_data_client_thread_cnt), start_params_, app);
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
                                            start_params_.ssl_cert_, start_params_.max_message_size_);
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
    if (_state_mgr_types.end() == it) { LOGE("Could not register [group_type={}]", group_type); }
}

nuraft::cb_func::ReturnCode ManagerImpl::generic_raft_event_handler(group_id_t const& group_id,
                                                                    nuraft::cb_func::Type type,
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
    return nuraft::cb_func::ReturnCode::Ok;
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
                LOGD("Creating new State Manager for: [group={}], type: {}", group_id, group_type);
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
        if (auto sp = wp.lock(); sp) { return sp->internal_raft_event_handler(group_id, type, param); }
        return nuraft::cb_func::Ok;
    });

    return nuraft::cmd_result_code::OK;
}

NullAsyncResult ManagerImpl::add_member(group_id_t const& group_id, peer_id_t const& new_id) {
    auto str_id = to_string(new_id);
    return _mesg_service->add_member(group_id, nuraft::srv_config(to_server_id(new_id), str_id))
        .deferValue([this, g_id = group_id, n_id = std::move(str_id)](auto cmd_result) mutable -> NullResult {
            if (!cmd_result) return folly::makeUnexpected(cmd_result.error());
            // TODO This should not block, but attach a new promise!
            auto lk = std::unique_lock< std::mutex >(_manager_lock);
            if (!_config_change.wait_for(
                    lk, std::chrono::milliseconds(NURAFT_MESG_CONFIG(raft_leader_change_timeout_ms)),
                    [this, g_id = std::move(g_id), n_id = std::move(n_id)]() {
                        std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
                        _mesg_service->get_srv_config_all(g_id, srv_list);
                        return std::find_if(srv_list.begin(), srv_list.end(),
                                            [n_id = std::move(n_id)](const std::shared_ptr< nuraft::srv_config >& cfg) {
                                                return n_id == cfg->get_endpoint();
                                            }) != srv_list.end();
                    })) {
                return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);
            }
            return folly::Unit();
        });
}

NullAsyncResult ManagerImpl::rem_member(group_id_t const& group_id, peer_id_t const& old_id) {
    return _mesg_service->rem_member(group_id, to_server_id(old_id));
}

NullAsyncResult ManagerImpl::become_leader(group_id_t const& group_id) {
    {
        auto lk = std::unique_lock< std::mutex >(_manager_lock);
        if (_is_leader[group_id]) { return folly::Unit(); }
    }

    return folly::makeSemiFuture< folly::Unit >(folly::Unit())
        .deferValue([this, g_id = group_id](auto) mutable -> NullResult {
            if (!_mesg_service->become_leader(g_id)) return folly::makeUnexpected(nuraft::cmd_result_code::CANCELLED);

            auto lk = std::unique_lock< std::mutex >(_manager_lock);
            if (!_config_change.wait_for(lk,
                                         std::chrono::milliseconds(NURAFT_MESG_CONFIG(raft_leader_change_timeout_ms)),
                                         [this, g_id = std::move(g_id)]() { return _is_leader[g_id]; }))
                return folly::makeUnexpected(nuraft::cmd_result_code::TIMEOUT);
            return folly::Unit();
        });
}

NullAsyncResult ManagerImpl::append_entries(group_id_t const& group_id,
                                            std::vector< std::shared_ptr< nuraft::buffer > > const& buf) {
    return _mesg_service->append_entries(group_id, buf);
}

std::shared_ptr< mesg_state_mgr > ManagerImpl::lookup_state_manager(group_id_t const& group_id) const {
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) return it->second;
    return nullptr;
}

NullAsyncResult ManagerImpl::create_group(group_id_t const& group_id, std::string const& group_type_name) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _is_leader.insert(std::make_pair(group_id, false));
    }
    if (auto const err = _mesg_service->joinRaftGroup(_srv_id, group_id, group_type_name); err) {
        return folly::makeUnexpected(err);
    }

    // Wait for the leader election timeout to make us the leader
    return folly::makeSemiFuture< folly::Unit >(folly::Unit())
        .deferValue([this, g_id = group_id](auto) mutable -> NullResult {
            auto lk = std::unique_lock< std::mutex >(_manager_lock);
            if (!_config_change.wait_for(lk,
                                         std::chrono::milliseconds(NURAFT_MESG_CONFIG(raft_leader_change_timeout_ms)),
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
    if (auto const err = _mesg_service->joinRaftGroup(_srv_id, group_id, group_type); err) {
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
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) { return it->second->get_logstore_id(); }
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
    if (auto const [handled, ret] = handle_raft_event(type, param); handled) { return ret; }
    if (auto sp = m_manager.lock(); sp) { return sp->generic_raft_event_handler(group_id, type, param); }
    return nuraft::cb_func::Ok;
}

std::shared_ptr< Manager > init_messaging(Manager::Params const& p, std::weak_ptr< MessagingApplication > w,
                                          bool with_data_svc) {
    RELEASE_ASSERT(w.lock(), "Could not acquire application!");
    auto m = std::make_shared< ManagerImpl >(p, w);
    m->start(with_data_svc);
    return m;
}

} // namespace nuraft_mesg
