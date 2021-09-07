#pragma once

#include <condition_variable>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <system_error>

#include <sds_logging/logging.h>

#include "messaging.h"

namespace sds {
namespace grpc {
class GrpcServer;
}

namespace messaging {
class group_factory;
class msg_service;
class group_metrics;
} // namespace messaging
} // namespace sds

namespace sds::messaging {

class consensus_impl : public consensus_component {
    std::string _node_id;
    int32_t _srv_id;

    consensus_component::create_state_mgr_cb _create_state_mgr_func;

    std::shared_ptr<::sds::messaging::group_factory > _g_factory;
    std::shared_ptr<::sds::messaging::msg_service > _mesg_service;
    std::unique_ptr<::sds::grpc::GrpcServer > _grpc_server;

    std::mutex mutable _manager_lock;
    std::map< std::string, std::shared_ptr< mesg_state_mgr > > _state_managers;

    std::condition_variable _leadership_change;
    std::map< std::string, bool > _is_leader;

    nuraft::ptr< nuraft::delayed_task_scheduler > _scheduler;
    std::shared_ptr< sds_logging::logger_t > _custom_logger;

    std::error_condition group_init(int32_t const srv_id, std::string const& group_id, nuraft::context*& ctx,
                                    std::shared_ptr< sds::messaging::group_metrics > metrics,
                                    sds::messaging::msg_service* sds_msg);
    nuraft::cb_func::ReturnCode callback_handler(std::string const& group_id, nuraft::cb_func::Type type,
                                                 nuraft::cb_func::Param* param);

public:
    consensus_impl();
    ~consensus_impl() override;

    int32_t server_id() const override { return _srv_id; }

    void start(consensus_component::params& start_params) override;
    bool add_member(std::string const& group_id, std::string const& server_id) override;
    bool rem_member(std::string const& group_id, std::string const& server_id) override;
    std::error_condition create_group(std::string const& group_id) override;
    void leave_group(std::string const& group_id) override;
    bool request_leadership(std::string const& group_id) override;
    std::error_condition join_group(std::string const& group_id, std::shared_ptr< mesg_state_mgr > smgr) override;
    std::error_condition client_request(std::string const& group_id, std::shared_ptr< nuraft::buffer >& buf) override;
    uint32_t logstore_id(std::string const& group_id) const override ;
    void get_peers(std::string const& group_id, std::list< std::string >&) const override;
};

} // namespace sds::messaging
