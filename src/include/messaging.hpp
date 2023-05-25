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
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <system_error>

#include <sisl/logging/logging.h>

#include "messaging_if.hpp"

namespace sisl {
class GrpcServer;
} // namespace sisl

namespace nuraft_mesg {
class group_factory;
class msg_service;
class group_metrics;

class service : public consensus_component {
    consensus_component::params _start_params;
    int32_t _srv_id;

    std::map< std::string, consensus_component::register_params > _state_mgr_types;

    std::shared_ptr< group_factory > _g_factory;
    std::shared_ptr< msg_service > _mesg_service;
    std::unique_ptr< ::sisl::GrpcServer > _grpc_server;

    std::mutex mutable _manager_lock;
    std::map< std::string, std::shared_ptr< mesg_state_mgr > > _state_managers;

    std::condition_variable _config_change;
    std::map< std::string, bool > _is_leader;

    nuraft::ptr< nuraft::delayed_task_scheduler > _scheduler;
    std::shared_ptr< sisl::logging::logger_t > _custom_logger;

    std::error_condition group_init(int32_t const srv_id, std::string const& group_id, std::string const& group_type,
                                    nuraft::context*& ctx, std::shared_ptr< group_metrics > metrics);
    nuraft::cb_func::ReturnCode callback_handler(std::string const& group_id, nuraft::cb_func::Type type,
                                                 nuraft::cb_func::Param* param);
    void exit_group(std::string const& group_id);

public:
    service();
    ~service() override;

    int32_t server_id() const override { return _srv_id; }

    void register_mgr_type(std::string const& group_type, register_params& params) override;

    std::shared_ptr< mesg_state_mgr > lookup_state_manager(std::string const& group_id) const override;

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
    void restart_server() override;

    // data service APIs
    bool bind_data_service_request(std::string const& request_name, std::string const& group_id,
                                   data_service_request_handler_t const& request_handler) override;

    // for testing
    void get_srv_config_all(std::string const& group_name,
                            std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out);
};

class repl_service_ctx_grpc : public repl_service_ctx {
public:
    repl_service_ctx_grpc(grpc_server* server, std::shared_ptr< mesg_factory > const& cli_factory);
    ~repl_service_ctx_grpc() override = default;
    std::shared_ptr< mesg_factory > m_mesg_factory;

    std::error_condition data_service_request(std::string const& request_name, io_blob_list_t const& cli_buf,
                                              data_service_response_handler_t const& response_cb) override;
    void send_data_service_response(io_blob_list_t const& outgoing_buf, void* rpc_data) override;
};

} // namespace nuraft_mesg
