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

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <exec/timed_thread_scheduler.hpp>

#include "nuraft_mesg/nuraft_mesg.hpp"
#include "lib/mesg_factory.hpp"
#include <sisl/logging/logging.h>
#include <libnuraft/nuraft.hxx>

#include "common_lib.hpp"
#include "async_helpers.hpp"

namespace sisl {
class GrpcServer;
} // namespace sisl

namespace nuraft_mesg {
class group_factory;
class msg_service;
class group_metrics;

class ManagerImpl : public manager, public std::enable_shared_from_this< ManagerImpl > {
    manager::params start_params_;
    int32_t _srv_id;

    std::map< group_type_t, manager::group_params > _state_mgr_types;

    std::weak_ptr< messaging_application > application_;
    std::shared_ptr< group_factory > _g_factory;

    // Protected
    std::mutex mutable _manager_lock;
    std::shared_ptr< msg_service > _mesg_service;
    std::unique_ptr< ::sisl::GrpcServer > _grpc_server;
    std::map< group_id_t, std::shared_ptr< mesg_state_mgr > > _state_managers;
    // Coroutine wakeups for control-plane waits (replaces the old _config_change condition_variable). Each
    // pending wait registers a wakeup_event under _manager_lock; the raft event handler signals all of a
    // group's waiters on a config/leadership change, and a per-wait deadline timer signals them on timeout.
    // _timer_ctx owns one background thread that fires those deadline timers.
    std::map< group_id_t, std::vector< std::shared_ptr< wakeup_event > > > _waiters;
    exec::timed_thread_context _timer_ctx;
    std::map< group_id_t, bool > _is_leader;
    //

    nuraft::ptr< nuraft::delayed_task_scheduler > _scheduler;
    std::shared_ptr< sisl::logging::logger_t > _custom_logger;

    void exit_group(group_id_t const& group_id);

    // Wake every coroutine waiting on a config/leadership change for this group (replaces
    // _config_change.notify_all()). Called from the raft event handler on a nuraft thread.
    void signal_waiters(group_id_t const& group_id);
    // Suspend until pred() holds or the deadline passes, woken by signal_waiters or a deadline timer.
    // Returns {} on success, std::unexpected(timeout_code) if the deadline is reached first.
    null_async_task wait_for_condition(group_id_t group_id, std::function< bool() > pred,
                                     std::chrono::steady_clock::time_point deadline,
                                     nuraft::cmd_result_code timeout_code);
    // Re-dispatch `dispatch` (a thunk producing a fresh attempt) while it returns CONFIG_CHANGING /
    // SERVER_IS_JOINING, backing off, until the deadline; returns the final raw code. Replaces the
    // CONFIG_CHANGING retry that the consumer (homestore) used to wrap around these calls.
    sisl::async::task< nuraft::cmd_result_code >
    retry_config_changing(std::function< sisl::async::task< nuraft::cmd_result_code >() > dispatch,
                          std::chrono::steady_clock::time_point deadline);
    // The add_member coroutine owns a CLONE of the srv_config (serialized into cfg_buf eagerly by the
    // public add_member, then deserialized into the frame): srv_config is non-copyable and the public
    // overload's const& would dangle once the (lazy) coroutine is co_awaited later (the two-statement
    // `auto t = add_member(...); co_await t;` pattern).
    null_async_task add_member_impl(group_id_t group_id, nuraft::ptr< nuraft::buffer > cfg_buf);

public:
    ManagerImpl(manager::params const&, std::weak_ptr< messaging_application >);
    ~ManagerImpl() override;

    // Public API
    void register_mgr_type(group_type_t const& group_type, group_params const&) override;

    std::shared_ptr< mesg_state_mgr > lookup_state_manager(group_id_t const& group_id) const override;
    null_async_task create_group(group_id_t const& group_id, group_type_t const& group_type) override;
    null_result join_group(group_id_t const& group_id, group_type_t const& group_type,
                          std::shared_ptr< mesg_state_mgr > smgr) override;

    null_async_task add_member(group_id_t const& group_id, peer_id_t const& server_id) override;
    null_async_task add_member(group_id_t const& group_id, nuraft::srv_config const& srv_config) override;
    null_async_task rem_member(group_id_t const& group_id, peer_id_t const& server_id) override;
    null_async_task become_leader(group_id_t const& group_id) override;
    null_async_task append_entries(group_id_t const& group_id,
                                 std::vector< std::shared_ptr< nuraft::buffer > > const&) override;

    void get_srv_config_all(group_id_t const& group_id,
                            std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out) override;
    void leave_group(group_id_t const& group_id) override;
    void append_peers(group_id_t const& group_id, std::list< peer_id_t >&) const override;
    uint32_t logstore_id(group_id_t const& group_id) const override;
    int32_t server_id() const override { return _srv_id; }
    void restart_server() override;

    bool bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                   data_service_request_handler_t const& request_handler) override;
    //

    /// Internal API
    nuraft::cmd_result_code group_init(int32_t const srv_id, group_id_t const& group_id, group_type_t const& group_type,
                                       nuraft::context*& ctx, std::shared_ptr< group_metrics > metrics);
    void start(bool and_data_svc);
    void generic_raft_event_handler(group_id_t const& group_id, nuraft::cb_func::Type type,
                                    nuraft::cb_func::Param* param);

    //
};

} // namespace nuraft_mesg
