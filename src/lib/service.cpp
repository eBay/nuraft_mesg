#include "service.hpp"

#include <boost/functional/hash.hpp>
#include <grpcpp/impl/codegen/status_code_enum.h>
#include <libnuraft/async.hxx>
#include <libnuraft/rpc_listener.hxx>
#include <sisl/options/options.h>

#include "nuraft_mesg/mesg_factory.hpp"
#include "nuraft_mesg/mesg_state_mgr.hpp"
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

msg_service::msg_service(std::shared_ptr< ManagerImpl > const& manager, group_id_t const& service_address,
                         std::string const& default_group_type, bool const enable_data_service) :
        _data_service_enabled(enable_data_service),
        _default_group_type(default_group_type),
        _manager(manager),
        _service_address(service_address) {}

msg_service::~msg_service() = default;

void msg_service::associate(::sisl::GrpcServer* server) {
    RELEASE_ASSERT(server, "NULL server!");
    if (_data_service_enabled) {
        _data_service.set_grpc_server(server);
        _data_service.associate();
    }
}

void msg_service::bind(::sisl::GrpcServer* server) {
    RELEASE_ASSERT(server, "NULL server!");
    if (_data_service_enabled) { _data_service.bind(); }
}

bool msg_service::bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                            data_service_request_handler_t const& request_handler) {
    if (!_data_service_enabled) {
        LOGE("Could not register data service method {}; data service is null", request_name);
        return false;
    }
    return _data_service.bind(request_name, group_id, request_handler);
}

NullAsyncResult msg_service::add_member(group_id_t const& group_id, nuraft::srv_config const& cfg) {
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
        CONTINUE_RESP(it->second.m_server->add_srv(cfg))
    }
    return folly::makeUnexpected(nuraft::SERVER_NOT_FOUND);
}

NullAsyncResult msg_service::rem_member(group_id_t const& group_id, int const member_id) {
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
        CONTINUE_RESP(it->second.m_server->rem_srv(member_id))
    }
    return folly::makeUnexpected(nuraft::SERVER_NOT_FOUND);
}

bool msg_service::become_leader(group_id_t const& group_id) {
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
        try {
            return it->second.m_server->request_leadership();
        } catch (std::runtime_error& rte) { LOGE("Caught exception during request_leadership(): {}", rte.what()) }
    }
    LOGW("Unknown [group={}] cannot get config.", group_id);
    return false;
}

void msg_service::get_srv_config_all(group_id_t const& group_id,
                                     std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out) {
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
        try {
            it->second.m_server->get_srv_config_all(configs_out);
        } catch (std::runtime_error& rte) { LOGE("Caught exception during add_srv(): {}", rte.what()); }
    } else {
        LOGW("Unknown [group={}] cannot get config.", group_id);
    }
}

NullAsyncResult msg_service::append_entries(group_id_t const& group_id,
                                            std::vector< nuraft::ptr< nuraft::buffer > > const& logs) {
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
        CONTINUE_RESP(it->second.m_server->append_entries(logs))
    }
    return folly::makeUnexpected(nuraft::SERVER_NOT_FOUND);
}

class msg_group_listner : public nuraft::rpc_listener {
    std::weak_ptr< msg_service > _svc;
    group_id_t _group;

public:
    msg_group_listner(std::shared_ptr< msg_service > const& svc, group_id_t const& group) : _svc(svc), _group(group) {}
    ~msg_group_listner() {
        if (auto svc = _svc.lock(); svc) svc->shutdown_for(_group);
    }

    void listen(nuraft::ptr< nuraft::msg_handler >&) override { LOGI("[group={}]", _group); }
    void stop() override { LOGI("[group={}]", _group); }
    void shutdown() override { LOGI("[group={}]", _group); }
};

void msg_service::shutdown_for(group_id_t const& group_id) {
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
        LOGD("Shutting down [group={}]", group_id);
        _raft_servers.erase(it);
    } else {
        LOGW("Unknown [group={}] cannot shutdown.", group_id);
    }
}

nuraft::cmd_result_code msg_service::joinRaftGroup(int32_t const srv_id, group_id_t const& group_id,
                                                   group_type_t const& group_type) {
    LOGI("Joining RAFT [group={}], type: {}", group_id, group_type);
    auto mgr = _manager.lock();
    if (!mgr) {
        LOGW("Got join after shutdown...skipping [group={}]", group_id);
        return nuraft::cmd_result_code::CANCELLED;
    }

    // This is for backwards compatibility for groups that had no type before.
    auto const g_type = group_type.empty() ? _default_group_type : group_type;
    // Quick check for duplicate, this will not guarantee we do not instantiate
    // more than one state_mgr, but it will quickly be destroyed
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) return nuraft::cmd_result_code::OK;

    auto metrics = std::shared_ptr< group_metrics >();
    if (0 < SISL_OPTIONS.count("msg_metrics")) { metrics = std::make_shared< group_metrics >(group_id); }

    nuraft::context* ctx{nullptr};
    if (auto err = mgr->group_init(srv_id, group_id, g_type, ctx, metrics); err) {
        LOGE("Error during RAFT server creation [group={}]: {}", group_id, err);
        return err;
    }

    DEBUG_ASSERT(!ctx->rpc_listener_, "RPC listner should not be set!");
    auto new_listner = std::make_shared< msg_group_listner >(shared_from_this(), group_id);
    ctx->rpc_listener_ = std::static_pointer_cast< nuraft::rpc_listener >(new_listner);
    auto server = std::make_shared< nuraft::raft_server >(ctx);
    if (auto [it, happened] = _raft_servers.try_emplace(group_id, metrics, std::make_unique< grpc_server >(server));
        happened) {
        if (_data_service_enabled) {
            auto smgr = std::dynamic_pointer_cast< mesg_state_mgr >(ctx->state_mgr_);
            auto cli_factory = std::dynamic_pointer_cast< mesg_factory >(ctx->rpc_cli_factory_);
            smgr->make_repl_ctx(it->second.m_server.get(), cli_factory);
        }
    } else {
        RELEASE_ASSERT(_raft_servers.end() != it, "FAILED to add a new raft server!");
    }
    return nuraft::cmd_result_code::OK;
}

void msg_service::leave_group(group_id_t const& group_id) {
    if (auto it = _raft_servers.find(group_id); _raft_servers.end() != it) {
        it->second.m_server->raft_server()->stop_server();
        it->second.m_server->raft_server()->shutdown();
    } else {
        LOGW("Unknown [group={}] cannot part.", group_id);
    }
}

void msg_service::shutdown() {
    LOGI("MessagingService shutdown started.");
    for (auto& [k, v] : _raft_servers) {
        v.m_server->raft_server()->stop_server();
        v.m_server->raft_server()->shutdown();
    }
}

} // namespace nuraft_mesg
