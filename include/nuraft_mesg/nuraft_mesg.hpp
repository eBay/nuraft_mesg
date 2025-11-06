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

#include <list>
#include <memory>
#include <string>
#include <vector>

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <libnuraft/raft_params.hxx>

#include "common.hpp"
#include <sisl/version.hpp>

namespace grpc {
class ByteBuffer;
class Status;
} // namespace grpc

namespace nuraft {
class buffer;
class srv_config;
} // namespace nuraft

namespace sisl {
class GenericRpcData;
class GrpcTokenVerifier;
class GrpcTokenClient;
} // namespace sisl

namespace nuraft_mesg {

class mesg_state_mgr;

// called by the server after it receives the request
using data_service_request_handler_t = std::function< void(boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) >;

class MessagingApplication {
public:
    virtual ~MessagingApplication() = default;
    MessagingApplication();
    virtual std::string lookup_peer(peer_id_t const&) = 0;
    virtual std::shared_ptr< mesg_state_mgr > create_state_mgr(int32_t const srv_id, group_id_t const& group_id) = 0;
};

class Manager {
public:
    struct Params {
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
    virtual ~Manager() = default;

    // Register a new group type
    virtual void register_mgr_type(group_type_t const& group_type, group_params const&) = 0;

    virtual std::shared_ptr< mesg_state_mgr > lookup_state_manager(group_id_t const& group_id) const = 0;
    virtual NullAsyncResult create_group(group_id_t const& group_id, group_type_t const& group_type) = 0;
    virtual NullResult join_group(group_id_t const& group_id, group_type_t const& group_type,
                                  std::shared_ptr< mesg_state_mgr >) = 0;

    // Send a client request to the cluster
    virtual NullAsyncResult add_member(group_id_t const& group_id, peer_id_t const& server_id) = 0;
    virtual NullAsyncResult add_member(group_id_t const& group_id, nuraft::srv_config const& srv_config) = 0;
    virtual NullAsyncResult rem_member(group_id_t const& group_id, peer_id_t const& server_id) = 0;
    virtual NullAsyncResult become_leader(group_id_t const& group_id) = 0;
    virtual NullAsyncResult append_entries(group_id_t const& group_id,
                                           std::vector< std::shared_ptr< nuraft::buffer > > const&) = 0;

    // Misc Mgmt
    virtual void get_srv_config_all(group_id_t const& group_id,
                                    std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out) = 0;
    virtual void leave_group(group_id_t const& group_id) = 0;
    virtual void append_peers(group_id_t const& group_id, std::list< peer_id_t >&) const = 0;
    virtual uint32_t logstore_id(group_id_t const& group_id) const = 0;
    virtual int32_t server_id() const = 0;
    virtual void restart_server() = 0;

    // data channel APIs
    virtual bool bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                           data_service_request_handler_t const&) = 0;
};

extern int32_t to_server_id(peer_id_t const& server_addr);

extern std::shared_ptr< Manager > init_messaging(Manager::Params const&, std::weak_ptr< MessagingApplication >,
                                                 bool with_data_svc = false);

} // namespace nuraft_mesg
