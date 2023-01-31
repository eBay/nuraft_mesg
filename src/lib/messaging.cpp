/// Copyright 2018 (c) eBay Corporation
//
#include "messaging.hpp"

#include <chrono>

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <ios>
#include <spdlog/fmt/ostr.h>
#include <spdlog/details/registry.h>

#include <libnuraft/async.hxx>
#include <sisl/options/options.h>
#include <sisl/grpc/rpc_server.hpp>

#include "service.hpp"
#include "mesg_factory.hpp"
#include "logger.hpp"

SISL_LOGGING_DECL(nuraft_mesg)

constexpr auto cfg_change_timeout = std::chrono::milliseconds(200);
constexpr auto leader_change_timeout = std::chrono::milliseconds(3200);
constexpr auto grpc_client_threads = 1u;
constexpr auto grpc_server_threads = 1u;

namespace nuraft_mesg {

class engine_factory : public group_factory {
    consensus_component::lookup_peer_cb _lookup_endpoint_func;

public:
    engine_factory(int const threads, consensus_component::params& start_params) :
            group_factory::group_factory(threads, start_params.server_uuid, start_params.trf_client,
                                         start_params.ssl_cert),
            _lookup_endpoint_func(start_params.lookup_peer) {
        DEBUG_ASSERT(!!_lookup_endpoint_func, "Lookup endpoint function NULL!");
    }

    std::string lookupEndpoint(std::string const& client) override { return _lookup_endpoint_func(client); }
};

service::service() = default;

service::~service() {
    if (_mesg_service) {
        _grpc_server->shutdown();
        _mesg_service->shutdown();
    }
}

void service::start(consensus_component::params& start_params) {
    _start_params = start_params;
    boost::hash< boost::uuids::uuid > uuid_hasher;
    _srv_id = uuid_hasher(boost::uuids::string_generator()(_start_params.server_uuid)) >> 33;

    _g_factory = std::make_shared< engine_factory >(grpc_client_threads, _start_params);
    auto logger_name = fmt::format("nuraft_{}", _start_params.server_uuid);
    //
    // NOTE: The Unit tests require this instance to be recreated with the same parameters.
    // This exception is only expected in this case where we "restart" the server by just recreating the instance.
    try {
        _custom_logger = sisl::logging::CreateCustomLogger(logger_name, "", false, false /* tee_to_stdout_stderr */);
    } catch (spdlog::spdlog_ex const& e) { _custom_logger = spdlog::details::registry::instance().get(logger_name); }

    sisl::logging::SetLogPattern("[%D %T.%f] [%^%L%$] [%t] %v", _custom_logger);
    nuraft::ptr< nuraft::logger > logger = std::make_shared< nuraft_mesg_logger >("scheduler", _custom_logger);

    // RAFT request scheduler
    nuraft::asio_service::options service_options;
    service_options.thread_pool_size_ = 1;
    _scheduler = std::make_shared< nuraft::asio_service >(service_options, logger);

    // The function passed to msg_service will be called each time a new group is joined,
    // allowing sharing of the Server and client amongst raft instances.

    _mesg_service = msg_service::create(
        [this](int32_t const srv_id, group_name_t const& group_id, group_type_t const& group_type,
               nuraft::context*& ctx, std::shared_ptr< group_metrics > metrics) mutable -> std::error_condition {
            return this->group_init(srv_id, group_id, group_type, ctx, metrics);
        },
        [this](group_type_t const& group_type) -> process_req_cb {
            std::lock_guard< std::mutex > lg(_manager_lock);
            auto const& type_params = _state_mgr_types[group_type];
            return type_params.process_req;
        },
        _start_params.server_uuid, _start_params.enable_data_service);
    _mesg_service->setDefaultGroupType(_start_params.default_group_type);

    // Start a gRPC server and create and associate nuraft_mesg services.
    restart_server();
}

void service::restart_server() {
    auto listen_address = fmt::format(FMT_STRING("0.0.0.0:{}"), _start_params.mesg_port);
    LOGINFO("Starting Messaging Service on http://{}", listen_address);

    std::lock_guard< std::mutex > lg(_manager_lock);
    _grpc_server.reset();
    _grpc_server = std::unique_ptr< sisl::GrpcServer >(sisl::GrpcServer::make(
        listen_address, _start_params.auth_mgr, grpc_server_threads, _start_params.ssl_key, _start_params.ssl_cert));
    _mesg_service->associate(_grpc_server.get());

    _grpc_server->run();
    _mesg_service->bind(_grpc_server.get());
}

void service::register_mgr_type(std::string const& group_type, register_params& params) {
    std::lock_guard< std::mutex > lg(_manager_lock);
    auto [it, happened] = _state_mgr_types.emplace(std::make_pair(group_type, params));
    DEBUG_ASSERT(_state_mgr_types.end() != it, "Out of memory?");
    DEBUG_ASSERT(!!happened, "Re-register?");
    if (_state_mgr_types.end() == it) { LOGERROR("Could not register group type: {}", group_type); }
}

nuraft::cb_func::ReturnCode service::callback_handler(std::string const& group_id, nuraft::cb_func::Type type,
                                                      nuraft::cb_func::Param* param) {
    switch (type) {
    case nuraft::cb_func::RemovedFromCluster: {
        LOGINFO("Removed from cluster {}", group_id);
        exit_group(group_id);
    } break;
    case nuraft::cb_func::JoinedCluster: {
        auto const my_id = param->myId;
        auto const leader_id = param->leaderId;
        LOGINFO("Joined cluster: {}, [l_id:{},my_id:{}]", group_id, leader_id, my_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = (leader_id == my_id);
        }
    } break;
    case nuraft::cb_func::NewConfig: {
        LOGDEBUGMOD(nuraft_mesg, "Cluster change for: {}", group_id);
        _config_change.notify_all();
    } break;
    case nuraft::cb_func::BecomeLeader: {
        LOGDEBUGMOD(nuraft_mesg, "I'm the leader of: {}!", group_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = true;
        }
        _config_change.notify_all();
    } break;
    case nuraft::cb_func::BecomeFollower: {
        LOGDEBUGMOD(nuraft_mesg, "I'm a follower of: {}!", group_id);
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

void service::exit_group(std::string const& group_id) {
    std::shared_ptr< mesg_state_mgr > mgr;
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (auto it = _state_managers.find(group_id); it != _state_managers.end()) { mgr = it->second; }
    }
    if (mgr) mgr->leave();
}

std::error_condition service::group_init(int32_t const srv_id, std::string const& group_id,
                                         std::string const& group_type, nuraft::context*& ctx,
                                         std::shared_ptr< nuraft_mesg::group_metrics > metrics) {
    LOGDEBUGMOD(nuraft_mesg, "Creating context for Group: {} as Member: {}", group_id, srv_id);

    // State manager (RAFT log store, config)
    std::shared_ptr< nuraft::state_mgr > smgr;
    std::shared_ptr< nuraft::state_machine > sm;
    nuraft::raft_params params;
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        auto const& type_params = _state_mgr_types[group_type];
        params = type_params.raft_params;

        auto [it, happened] = _state_managers.emplace(group_id, nullptr);
        if (it != _state_managers.end()) {
            if (happened) {
                // A new logstore!
                LOGDEBUGMOD(nuraft_mesg, "Creating new State Manager for: {}, type: {}", group_id, group_type);
                it->second = type_params.create_state_mgr(srv_id, group_id);
            }
            it->second->become_ready();
            sm = it->second->get_state_machine();
            smgr = it->second;
        } else {
            return std::make_error_condition(std::errc::not_enough_memory);
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
        return this->callback_handler(group_id, type, param);
    });

    return std::error_condition();
}

bool service::add_member(std::string const& group_id, std::string const& server_id) {
    return add_member(group_id, server_id, false);
}

bool service::add_member(std::string const& group_id, std::string const& server_id, bool const wait_for_completion) {
    boost::hash< boost::uuids::uuid > uuid_hasher;
    int32_t const srv_id = uuid_hasher(boost::uuids::string_generator()(server_id)) >> 33;
    auto cfg = nuraft::srv_config(srv_id, server_id);
    nuraft::cmd_result_code rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        rc = _mesg_service->add_srv(group_id, cfg);
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            LOGDEBUGMOD(nuraft_mesg, "Server is busy, retrying...");
            std::this_thread::sleep_for(cfg_change_timeout);
        }
    }
    if (nuraft::SERVER_NOT_FOUND == rc) {
        LOGWARN("Messaging service does not know of group: [{}]", group_id);
    } else if (nuraft::OK != rc) {
        LOGERROR("Unknown failure to add member: [{}]", static_cast< uint32_t >(rc));
    }

    if (!wait_for_completion) { return nuraft::OK == rc; }

    auto lk = std::unique_lock< std::mutex >(_manager_lock);
    return (nuraft::OK == rc) && _config_change.wait_for(lk, cfg_change_timeout * 20, [this, &group_id, &server_id]() {
        std::vector< std::shared_ptr< nuraft::srv_config > > srv_list;
        _mesg_service->get_srv_config_all(group_id, srv_list);
        return std::find_if(srv_list.begin(), srv_list.end(),
                            [&server_id](const std::shared_ptr< nuraft::srv_config >& cfg) {
                                return server_id == cfg->get_endpoint();
                            }) != srv_list.end();
    });
}

bool service::rem_member(std::string const& group_id, std::string const& server_id) {
    boost::hash< boost::uuids::uuid > uuid_hasher;
    int32_t const srv_id = uuid_hasher(boost::uuids::string_generator()(server_id)) >> 33;

    nuraft::cmd_result_code rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        rc = _mesg_service->rm_srv(group_id, srv_id);
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            LOGDEBUGMOD(nuraft_mesg, "Server is busy, retrying...");
            std::this_thread::sleep_for(cfg_change_timeout);
        }
    }
    if (nuraft::SERVER_NOT_FOUND == rc) {
        LOGWARN("Messaging service does not know of group: [{}]", group_id);
    } else if (nuraft::OK != rc) {
        LOGERROR("Unknown failure to add member: [{}]", static_cast< uint32_t >(rc));
    }
    return nuraft::OK == rc;
}

std::error_condition service::create_group(std::string const& group_id, std::string const& group_type_name) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _is_leader.insert(std::make_pair(group_id, false));
    }

    if (auto const err = _mesg_service->createRaftGroup(_srv_id, group_id, group_type_name); err) { return err; }

    // Wait for the leader election timeout to make us the leader
    auto lk = std::unique_lock< std::mutex >(_manager_lock);
    if (!_config_change.wait_for(lk, leader_change_timeout, [this, &group_id]() { return _is_leader[group_id]; })) {
        return std::make_error_condition(std::errc::timed_out);
    }
    return std::error_condition();
}

std::error_condition service::join_group(std::string const& group_id, std::string const& group_type,
                                         std::shared_ptr< mesg_state_mgr > smgr) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        auto [it, happened] = _state_managers.emplace(group_id, smgr);
        if (_state_managers.end() == it) return std::make_error_condition(std::errc::not_enough_memory);
    }
    if (auto const err = _mesg_service->joinRaftGroup(_srv_id, group_id, group_type); err) {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _state_managers.erase(group_id);
        return err;
    }
    std::this_thread::sleep_for(cfg_change_timeout);
    return std::error_condition();
}

void service::get_peers(std::string const& group_id, std::list< std::string >& servers) const {
    auto res = std::list< std::string >();
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) {
        if (auto config = it->second->load_config(); config) {
            for (auto const& server : config->get_servers()) {
                servers.push_back(server->get_endpoint());
            }
        }
    }
}

bool service::request_leadership(std::string const& group_id) {
    {
        auto lk = std::unique_lock< std::mutex >(_manager_lock);
        if (_is_leader[group_id]) { return true; }
    }

    bool request_success{false};
    for (auto max_retries = 5ul; max_retries > 0; --max_retries) {
        if (_mesg_service->request_leadership(group_id)) {
            request_success = true;
            break;
        }
        // Do not sleep on the last try
        if (max_retries != 1) { std::this_thread::sleep_for(std::chrono::milliseconds(leader_change_timeout)); }
    }
    auto lk = std::unique_lock< std::mutex >(_manager_lock);
    return request_success &&
        _config_change.wait_for(lk, leader_change_timeout, [this, &group_id]() { return _is_leader[group_id]; });
}

void service::leave_group(std::string const& group_id) {
    LOGINFO("Leaving group [vol={}]", group_id);
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (0 == _state_managers.count(group_id)) {
            LOGDEBUGMOD(nuraft_mesg, "Asked to leave group {} which we are not part of!", group_id);
            return;
        }
    }

    _mesg_service->partRaftGroup(group_id);

    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) {
        // Delete all the state files (RAFT log etc.) after descrtuctor is called.
        it->second->permanent_destroy();
        _state_managers.erase(it);
    }

    LOGINFO("Finished leaving: [vol={}]", group_id);
}

static std::error_condition convertToError(nuraft::cmd_result_code const& rc) {
    switch (rc) {
    case nuraft::OK:
        return std::error_condition();
    case nuraft::CANCELLED:
        return std::make_error_condition(std::errc::operation_canceled);
    case nuraft::TIMEOUT:
        return std::make_error_condition(std::errc::timed_out);
    case nuraft::NOT_LEADER:
        return std::make_error_condition(std::errc::permission_denied);
    case nuraft::BAD_REQUEST:
        return std::make_error_condition(std::errc::invalid_argument);
    case nuraft::SERVER_ALREADY_EXISTS:
        return std::make_error_condition(std::errc::file_exists);
    case nuraft::CONFIG_CHANGING:
        return std::make_error_condition(std::errc::interrupted);
    case nuraft::SERVER_IS_JOINING:
        return std::make_error_condition(std::errc::device_or_resource_busy);
    case nuraft::SERVER_NOT_FOUND:
        return std::make_error_condition(std::errc::no_such_device);
    case nuraft::CANNOT_REMOVE_LEADER:
        return std::make_error_condition(std::errc::not_supported);
    case nuraft::SERVER_IS_LEAVING:
        return std::make_error_condition(std::errc::owner_dead);
    case nuraft::FAILED:
        [[fallthrough]];
    default:
        return std::make_error_condition(std::errc::io_error);
    }
}

std::error_condition service::client_request(std::string const& group_id, std::shared_ptr< nuraft::buffer >& buf) {
    auto rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        LOGDEBUGMOD(nuraft_mesg, "Sending Client Request to {}", group_id);
        rc = _mesg_service->append_entries(group_id, {buf});
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            LOGDEBUGMOD(nuraft_mesg, "Server is busy, retrying...");
            std::this_thread::sleep_for(cfg_change_timeout);
        }
    }
    return convertToError(rc);
}
uint32_t service::logstore_id(std::string const& group_id) const {
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) { return it->second->get_logstore_id(); }
    return UINT32_MAX;
}

void service::get_srv_config_all(std::string const& group_name,
                                 std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out) {
    _mesg_service->get_srv_config_all(group_name, configs_out);
}

bool service::bind_data_service_request(std::string const& request_name,
                                        data_service_request_handler_t const& request_handler) {
    return _mesg_service->bind_data_service_request(_grpc_server.get(), request_name, request_handler);
}

std::error_condition service::data_service_request(std::string const& group_id, std::string const& request_name,
                                                   data_service_response_handler_t const& response_cb,
                                                   io_blob_list_t const& cli_buf) {
    return _mesg_service->data_service_request(group_id, request_name, response_cb, cli_buf);
}
} // namespace nuraft_mesg
