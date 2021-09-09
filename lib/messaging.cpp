/// Copyright 2018 (c) eBay Corporation
//
#include "messaging.h"

#include <chrono>

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <ios>
#include <spdlog/fmt/ostr.h>

#include <libnuraft/async.hxx>
#include <sds_options/options.h>
#include <grpc_helper/rpc_server.hpp>

#include "service.h"
#include "factory.h"
#include "logger.h"

SDS_OPTION_GROUP(messaging,
                 (snapshot_period, "", "snapshot_period", "Number of journal entries per snapshot.",
                  cxxopts::value< int32_t >()->default_value("1"), ""))

constexpr auto rpc_backoff = 50;
constexpr auto heartbeat_period = 100;
constexpr auto elect_to_low = heartbeat_period * 2;
constexpr auto elect_to_high = elect_to_low * 2;
constexpr auto cfg_change_timeout = std::chrono::milliseconds(elect_to_low);
constexpr auto leader_change_timeout = std::chrono::milliseconds(elect_to_high) * 8;
constexpr auto grpc_client_threads = 1u;
constexpr auto grpc_server_threads = 1u;

namespace sds::messaging {

class engine_factory : public group_factory {
    consensus_component::lookup_peer_cb _lookup_endpoint_func;

public:
    engine_factory(int const threads, consensus_component::params& start_params) :
            group_factory::group_factory(threads, start_params.server_uuid),
            _lookup_endpoint_func(start_params.lookup_peer) {
        DEBUG_ASSERT(!!_lookup_endpoint_func, "Lookup endpoint function NULL!");
    }

    std::string lookupEndpoint(std::string const& client) override { return _lookup_endpoint_func(client); }
};

consensus_impl::consensus_impl() = default;

consensus_impl::~consensus_impl() {
    if (_mesg_service) {
        _grpc_server->shutdown();
        _mesg_service->shutdown();
    }
}

void consensus_impl::start(consensus_component::params& start_params) {
    _node_id = start_params.server_uuid;
    boost::hash< boost::uuids::uuid > uuid_hasher;
    _srv_id = uuid_hasher(boost::uuids::string_generator()(_node_id)) >> 33;

    auto _listen_address = fmt::format(FMT_STRING("0.0.0.0:{}"), start_params.mesg_port);
    _g_factory = std::make_shared< engine_factory >(grpc_client_threads, start_params);
    _custom_logger = sds_logging::CreateCustomLogger(fmt::format("nuraft_{}", _node_id), "", false,
                                                     false /* tee_to_stdout_stderr */);
    sds_logging::SetLogPattern("[%D %T.%f] [%^%L%$] [%t] %v", _custom_logger);
    nuraft::ptr< nuraft::logger > logger = std::make_shared< sds_logger >("scheduler", _custom_logger);

    // RAFT request scheduler
    nuraft::asio_service::options service_options;
    service_options.thread_pool_size_ = 1;
    _scheduler = std::make_shared< nuraft::asio_service >(service_options, logger);

    LOGINFO("Starting Messaging Service on http://{}", _listen_address);

    // Start a gRPC server and create and associate sds_messaging service. The function
    // passed to msg_service will be called each time a new group is joined, allowing
    // sharing of the Server and client amongst raft instances.
    _grpc_server.reset(grpc_helper::GrpcServer::make(_listen_address, grpc_server_threads, "", ""));
    _mesg_service = msg_service::create(
        [this](int32_t const srv_id, group_name_t const& group_id, group_type_t const& group_type, nuraft::context*& ctx,
               std::shared_ptr< group_metrics > metrics, msg_service* sds_msg) mutable -> std::error_condition {
            return this->group_init(srv_id, group_id, group_type, ctx, metrics, sds_msg);
        },
        _node_id);

    _mesg_service->associate(_grpc_server.get());
    _grpc_server->run();
    _mesg_service->bind(_grpc_server.get());
} // namespace sds::messaging


void consensus_impl::register_mgr_type(std::string const& group_type, register_params& params) {
    std::lock_guard< std::mutex > lg(_manager_lock);
    auto [it, happened] = _create_state_mgr_funcs.emplace(std::make_pair(group_type, params.create_state_mgr));
    DEBUG_ASSERT(_create_state_mgr_funcs.end() != it, "Out of memory?");
    DEBUG_ASSERT(!!happened, "Re-register?");
    if (_create_state_mgr_funcs.end() == it) {
        LOGERROR("Could not register group type: {}", group_type);
    }
}

nuraft::cb_func::ReturnCode consensus_impl::callback_handler(std::string const& group_id, nuraft::cb_func::Type type,
                                                        nuraft::cb_func::Param* param) {
    switch (type) {
    case nuraft::cb_func::RemovedFromCluster: {
        LOGINFO("Removed from cluster {}", group_id);
    } break;
    case nuraft::cb_func::JoinedCluster: {
        auto const my_id = param->myId;
        auto const leader_id = param->leaderId;
        LOGINFO("Joined cluster: {}, [l_id:{},my_id:{}]", group_id, leader_id, my_id);
    } break;
    case nuraft::cb_func::NewConfig: {
        LOGDEBUG("Cluster change for: {}", group_id);
    } break;
    case nuraft::cb_func::BecomeLeader: {
        LOGDEBUG("I'm the leader of: {}!", group_id);
        {
            std::lock_guard< std::mutex > lg(_manager_lock);
            _is_leader[group_id] = true;
        }
        _leadership_change.notify_all();
    } break;
    case nuraft::cb_func::BecomeFollower: {
        LOGDEBUG("I'm a follower of: {}!", group_id);
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

std::error_condition consensus_impl::group_init(int32_t const srv_id, std::string const& group_id, std::string const& group_type, nuraft::context*& ctx,
                                           std::shared_ptr< sds::messaging::group_metrics > metrics,
                                           sds::messaging::msg_service* sds_msg) {
    LOGDEBUG("Creating context for Group: {} as Member: {}", group_id, srv_id);

    // State manager (RAFT log store, config)
    std::shared_ptr< nuraft::state_mgr > smgr;
    std::shared_ptr< nuraft::state_machine > sm;
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        auto [it, happened] = _state_managers.emplace(group_id, nullptr);
        if (it != _state_managers.end()) {
            if (happened) {
                // A new logstore!
                LOGDEBUG("Creating new State Manager for: {}", group_id);
                it->second = _create_state_mgr_funcs[group_type](srv_id, group_id);
            }
            it->second->become_ready();
            sm = it->second->get_state_machine();
            smgr = it->second;
        } else {
            return std::make_error_condition(std::errc::not_enough_memory);
        }
    }

    // RAFT server parameters
    nuraft::raft_params params;
    params.with_election_timeout_lower(elect_to_low)
        .with_election_timeout_upper(elect_to_high)
        .with_hb_interval(heartbeat_period)
        .with_max_append_size(10)
        .with_rpc_failure_backoff(rpc_backoff)
        .with_auto_forwarding(true)
        .with_snapshot_enabled(SDS_OPTIONS["snapshot_period"].as< int32_t >());

    // RAFT client factory
    std::shared_ptr< nuraft::rpc_client_factory > rpc_cli_factory(
        std::make_shared< sds::messaging::mesg_factory >(_g_factory, group_id, group_type, metrics));

    // RAFT service interface (stops gRPC service etc...) (TODO)
    std::shared_ptr< nuraft::rpc_listener > listener;

    nuraft::ptr< nuraft::logger > logger = std::make_shared< sds_logger >(group_id, _custom_logger);
    ctx = new nuraft::context(smgr, sm, listener, logger, rpc_cli_factory, _scheduler, params);
    ctx->set_cb_func([this, group_id](nuraft::cb_func::Type type, nuraft::cb_func::Param* param) mutable {
        return this->callback_handler(group_id, type, param);
    });

    return std::error_condition();
}

bool consensus_impl::add_member(std::string const& group_id, std::string const& server_id) {
    boost::hash< boost::uuids::uuid > uuid_hasher;
    int32_t const srv_id = uuid_hasher(boost::uuids::string_generator()(server_id)) >> 33;
    auto cfg = nuraft::srv_config(srv_id, server_id);
    nuraft::cmd_result_code rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        rc = _mesg_service->add_srv(group_id, cfg);
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            LOGDEBUG("Server is busy, retrying...");
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

bool consensus_impl::rem_member(std::string const& group_id, std::string const& server_id) {
    boost::hash< boost::uuids::uuid > uuid_hasher;
    int32_t const srv_id = uuid_hasher(boost::uuids::string_generator()(server_id)) >> 33;

    nuraft::cmd_result_code rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        rc = _mesg_service->rm_srv(group_id, srv_id);
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            LOGDEBUG("Server is busy, retrying...");
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

std::error_condition consensus_impl::create_group(std::string const& group_id, std::string const& group_type_name) {
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        _is_leader.insert(std::make_pair(group_id, false));
    }

    if (auto const err = _mesg_service->createRaftGroup(_srv_id, group_id, group_type_name); err) { return err; }

    // Wait for the leader election timeout to make us the leader
    auto lk = std::unique_lock< std::mutex >(_manager_lock);
    if (!_leadership_change.wait_for(lk, leader_change_timeout, [this, &group_id]() { return _is_leader[group_id]; })) {
        return std::make_error_condition(std::errc::timed_out);
    }
    return std::error_condition();
}

std::error_condition consensus_impl::join_group(std::string const& group_id, std::string const& group_type, std::shared_ptr< mesg_state_mgr > smgr) {
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

void consensus_impl::get_peers(std::string const& group_id, std::list< std::string >& servers) const {
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

bool consensus_impl::request_leadership(std::string const& group_id) {
    _mesg_service->request_leadership(group_id);

    auto lk = std::unique_lock< std::mutex >(_manager_lock);
    return _leadership_change.wait_for(lk, leader_change_timeout, [this, &group_id]() { return _is_leader[group_id]; });
}

void consensus_impl::leave_group(std::string const& group_id) {
    LOGINFO("Leaving group [vol={}]", group_id);
    {
        std::lock_guard< std::mutex > lg(_manager_lock);
        if (0 == _state_managers.count(group_id)) {
            LOGDEBUG("Asked to leave group {} which we are not part of!", group_id);
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

std::error_condition consensus_impl::client_request(std::string const& group_id, std::shared_ptr< nuraft::buffer >& buf) {
    auto rc = nuraft::SERVER_IS_JOINING;
    while (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
        LOGDEBUG("Sending Client Request to {}", group_id);
        rc = _mesg_service->append_entries(group_id, {buf});
        if (nuraft::SERVER_IS_JOINING == rc || nuraft::CONFIG_CHANGING == rc) {
            LOGDEBUG("Server is busy, retrying...");
            std::this_thread::sleep_for(cfg_change_timeout);
        }
    }
    return convertToError(rc);
}
uint32_t consensus_impl::logstore_id(std::string const& group_id) const {
    std::lock_guard< std::mutex > lg(_manager_lock);
    if (auto it = _state_managers.find(group_id); _state_managers.end() != it) { return it->second->get_logstore_id(); }
    return UINT32_MAX;
}
} // namespace sds::messaging
