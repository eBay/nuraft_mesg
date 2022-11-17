#pragma once

#include <condition_variable>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <system_error>

#include <sisl/logging/logging.h>

#include "messaging_if.h"

namespace grpc_helper {
class GrpcServer;
} // namespace grpc_helper

namespace sds::messaging {
class group_factory;
class msg_service;
class group_metrics;

class service : public consensus_component {
    std::string _node_id;
    int32_t _srv_id;

    std::map< std::string, consensus_component::register_params > _state_mgr_types;

    std::shared_ptr< ::sds::messaging::group_factory > _g_factory;
    std::shared_ptr< ::sds::messaging::msg_service > _mesg_service;
    std::unique_ptr< ::grpc_helper::GrpcServer > _grpc_server;

    std::mutex mutable _manager_lock;
    std::map< std::string, std::shared_ptr< mesg_state_mgr > > _state_managers;

    std::condition_variable _config_change;
    std::map< std::string, bool > _is_leader;

    nuraft::ptr< nuraft::delayed_task_scheduler > _scheduler;
    std::shared_ptr< sisl::logging::logger_t > _custom_logger;

    std::error_condition group_init(int32_t const srv_id, std::string const& group_id, std::string const& group_type,
                                    nuraft::context*& ctx, std::shared_ptr< sds::messaging::group_metrics > metrics);
    nuraft::cb_func::ReturnCode callback_handler(std::string const& group_id, nuraft::cb_func::Type type,
                                                 nuraft::cb_func::Param* param);
    void exit_group(std::string const& group_id);

public:
    service();
    ~service() override;

    int32_t server_id() const override { return _srv_id; }

    void register_mgr_type(std::string const& group_type, register_params& params) override;

    std::error_condition create_group(std::string const& group_id, std::string const& group_type) override;
    std::error_condition join_group(std::string const& group_id, std::string const& group_type,
                                    std::shared_ptr< mesg_state_mgr > smgr) override;

    void start(consensus_component::params& start_params) override;
    bool add_member(std::string const& group_id, std::string const& server_id) override;
    bool add_member(std::string const& group_id, std::string const& server_id, bool const wait_for_completion) override;
    bool rem_member(std::string const& group_id, std::string const& server_id) override;
    void leave_group(std::string const& group_id) override;
    bool request_leadership(std::string const& group_id) override;
    std::error_condition client_request(std::string const& group_id, std::shared_ptr< nuraft::buffer >& buf) override;
    uint32_t logstore_id(std::string const& group_id) const override;
    void get_peers(std::string const& group_id, std::list< std::string >&) const override;
    void restart_server(consensus_component::params& start_params) override;

    // for testing
    void get_srv_config_all(std::string const& group_name,
                            std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out);
};

} // namespace sds::messaging
