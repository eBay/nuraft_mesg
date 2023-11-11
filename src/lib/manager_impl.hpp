/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#include <condition_variable>
#include <libnuraft/async.hxx>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <sisl/logging/logging.h>

#include "nuraft_mesg/nuraft_mesg.hpp"
#include "nuraft_mesg/mesg_factory.hpp"
#include "common_lib.hpp"

namespace sisl {
class GrpcServer;
} // namespace sisl

namespace nuraft_mesg {
class group_factory;
class msg_service;
class group_metrics;

class ManagerImpl : public Manager, public std::enable_shared_from_this< ManagerImpl > {
    Manager::Params start_params_;
    int32_t _srv_id;

    std::map< group_type_t, Manager::group_params > _state_mgr_types;

    std::weak_ptr< MessagingApplication > application_;
    std::shared_ptr< group_factory > _g_factory;
    std::shared_ptr< msg_service > _mesg_service;
    std::unique_ptr< ::sisl::GrpcServer > _grpc_server;

    std::mutex mutable _manager_lock;
    std::map< group_id_t, std::shared_ptr< mesg_state_mgr > > _state_managers;

    std::condition_variable _config_change;
    std::map< group_id_t, bool > _is_leader;

    nuraft::ptr< nuraft::delayed_task_scheduler > _scheduler;
    std::shared_ptr< sisl::logging::logger_t > _custom_logger;

    void raft_event(group_id_t const& group_id, nuraft::cb_func::Type type, nuraft::cb_func::Param* param);
    void exit_group(group_id_t const& group_id);

public:
    ManagerImpl(Manager::Params const&, std::weak_ptr< MessagingApplication >);
    ~ManagerImpl() override;

    int32_t server_id() const override { return _srv_id; }

    void register_mgr_type(group_type_t const& group_type, group_params const&) override;

    nuraft::cmd_result_code group_init(int32_t const srv_id, group_id_t const& group_id, group_type_t const& group_type,
                                       nuraft::context*& ctx, std::shared_ptr< group_metrics > metrics);

    std::shared_ptr< mesg_state_mgr > lookup_state_manager(group_id_t const& group_id) const override;

    NullAsyncResult create_group(group_id_t const& group_id, group_type_t const& group_type) override;
    NullResult join_group(group_id_t const& group_id, group_type_t const& group_type,
                          std::shared_ptr< mesg_state_mgr > smgr) override;

    virtual NullAsyncResult add_member(group_id_t const& group_id, peer_id_t const& server_id) override;
    virtual NullAsyncResult rem_member(group_id_t const& group_id, peer_id_t const& server_id) override;
    virtual NullAsyncResult become_leader(group_id_t const& group_id) override;
    virtual NullAsyncResult append_entries(group_id_t const& group_id,
                                           std::vector< std::shared_ptr< nuraft::buffer > > const&) override;

    void leave_group(group_id_t const& group_id) override;
    uint32_t logstore_id(group_id_t const& group_id) const override;
    void append_peers(group_id_t const& group_id, std::list< peer_id_t >&) const override;
    void start(bool and_data_svc);
    void restart_server() override;

    // data service APIs
    bool bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                   data_service_request_handler_t const& request_handler) override;

    void get_srv_config_all(group_id_t const& group_id,
                            std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out) override;
};

} // namespace nuraft_mesg
