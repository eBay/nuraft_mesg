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

#include <functional>
#include <memory>
#include <string>

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <libnuraft/raft_params.hxx>

#include "common.hpp"
#include <sisl/version.hpp>

namespace nuraft {
class srv_config;
} // namespace nuraft

namespace sisl {
class GenericRpcData;
class GrpcTokenVerifier;
class GrpcTokenClient;
} // namespace sisl

namespace nuraft_mesg {

class mesg_state_mgr;
class ManagerImpl;

// called by the server after it receives the request
using data_service_request_handler_t = std::function< void(boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) >;

class messaging_application {
public:
    virtual ~messaging_application() = default;
    messaging_application();
    virtual std::string lookup_peer(peer_id_t const&) = 0;
    virtual std::shared_ptr< mesg_state_mgr > create_state_mgr(int32_t const srv_id, group_id_t const& group_id) = 0;
};

// Opaque handle returned by init_messaging. All methods delegate to the internal ManagerImpl; ManagerImpl
// is an incomplete type here so the implementation details stay fully hidden from consumers.
class manager {
public:
    struct params {
        boost::uuids::uuid server_uuid_;
        uint16_t mesg_port_;
        group_type_t default_group_type_;
        std::string ssl_key_;
        std::string ssl_cert_;
        std::string ssl_ca_;
        std::shared_ptr< sisl::GrpcTokenVerifier > token_verifier_{nullptr};
        std::shared_ptr< sisl::GrpcTokenClient > token_client_{nullptr};
        int max_receive_message_size_{0};
        int max_send_message_size_{0};
        bool enable_console_log_{false};
    };
    using group_params = nuraft::raft_params;

    explicit manager(std::shared_ptr< ManagerImpl > impl);
    ~manager();

    // Register a new group type
    void register_mgr_type(group_type_t const& group_type, group_params const&);

    std::shared_ptr< mesg_state_mgr > lookup_state_manager(group_id_t const& group_id) const;
    [[nodiscard]] null_async_task create_group(group_id_t const& group_id, group_type_t const& group_type);
    [[nodiscard]] null_result join_group(group_id_t const& group_id, group_type_t const& group_type,
                                        std::shared_ptr< mesg_state_mgr >);

    // Send a client request to the cluster
    [[nodiscard]] null_async_task add_member(group_id_t const& group_id, peer_id_t const& server_id);
    [[nodiscard]] null_async_task add_member(group_id_t const& group_id, nuraft::srv_config const& srv_config);
    [[nodiscard]] null_async_task rem_member(group_id_t const& group_id, peer_id_t const& server_id);
    [[nodiscard]] null_async_task become_leader(group_id_t const& group_id);

    // Misc Mgmt
    void leave_group(group_id_t const& group_id);
    int32_t server_id() const;
    void restart_server();

    // data channel APIs
    bool bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                   data_service_request_handler_t const&);

private:
    std::shared_ptr< ManagerImpl > impl_;
};

extern int32_t to_server_id(peer_id_t const& server_addr);

extern std::shared_ptr< manager > init_messaging(manager::params const&, std::weak_ptr< messaging_application >,
                                                 bool with_data_svc = false);

} // namespace nuraft_mesg
