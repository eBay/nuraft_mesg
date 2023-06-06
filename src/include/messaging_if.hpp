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
#include <system_error>
#include <folly/small_vector.h>

#include <libnuraft/nuraft.hxx>
#include <sisl/fds/buffer.hpp>

namespace grpc {
class ByteBuffer;
class Status;
} // namespace grpc

namespace sisl {
class AuthManager;
class TrfClient;
class GenericRpcData;
using generic_unary_callback_t = std::function< void(grpc::ByteBuffer&, ::grpc::Status& status) >;
} // namespace sisl

namespace boost {
template < class T >
class intrusive_ptr;
} // namespace boost

namespace nuraft_mesg {

using io_blob_list_t = folly::small_vector< sisl::io_blob, 4 >;

// called by the server after it receives the request
using data_service_request_handler_t =
    std::function< void(sisl::io_blob const& incoming_buf, boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) >;

// called by the client after it receives response to its request
using data_service_response_handler_t = std::function< void(sisl::io_blob const& incoming_buf) >;

// This object can be stored by the caller and can be used to directly call raft/data operatons without taking
// _raft_servers_lock
class mesg_factory;
class grpc_server;

class repl_service_ctx {
public:
    repl_service_ctx(grpc_server* server);
    virtual ~repl_service_ctx() = default;

    // we do not own this pointer. Use this only if the lyfe cycle of the pointer is well known
    grpc_server* m_server;
    bool is_raft_leader() const;

    // data service api client call
    virtual std::error_condition data_service_request(std::string const& request_name, io_blob_list_t const& cli_buf,
                                                      data_service_response_handler_t const& response_cb) = 0;

    // Send response to a data service request and finish the async call.
    virtual void send_data_service_response(io_blob_list_t const& outgoing_buf,
                                            boost::intrusive_ptr< sisl::GenericRpcData >& rpc_data) = 0;
};

class mesg_state_mgr : public nuraft::state_mgr {
public:
    using nuraft::state_mgr::state_mgr;
    virtual ~mesg_state_mgr() = default;
    void make_repl_ctx(grpc_server* server, std::shared_ptr< mesg_factory >& cli_factory);

    virtual void become_ready() {}
    virtual uint32_t get_logstore_id() const = 0;
    virtual std::shared_ptr< nuraft::state_machine > get_state_machine() = 0;
    virtual void permanent_destroy() = 0;
    virtual void leave() = 0;

protected:
    std::unique_ptr< repl_service_ctx > m_repl_svc_ctx;
};

extern int32_t to_server_id(std::string const& server_addr);

class consensus_component {
public:
    using lookup_peer_cb = std::function< std::string(std::string const&) >;
    using create_state_mgr_cb =
        std::function< std::shared_ptr< mesg_state_mgr >(int32_t const srv_id, std::string const& group_id) >;
    using process_req_cb = std::function< void(std::function< void() >) >;

    struct params {
        std::string server_uuid;
        uint32_t mesg_port;
        lookup_peer_cb lookup_peer;
        std::string default_group_type;
        std::shared_ptr< sisl::AuthManager > auth_mgr{nullptr};
        std::shared_ptr< sisl::TrfClient > trf_client{nullptr};
        std::string ssl_key{};
        std::string ssl_cert{};
        bool enable_data_service{false};
    };
    virtual ~consensus_component() = default;
    virtual void start(consensus_component::params& start_params) = 0;

    // Register a new state_mgr type
    struct register_params {
        nuraft::raft_params raft_params;
        create_state_mgr_cb create_state_mgr;
        process_req_cb process_req{nullptr};
    };
    virtual void register_mgr_type(std::string const& group_type, register_params& params) = 0;

    virtual std::shared_ptr< mesg_state_mgr > lookup_state_manager(std::string const& group_id) const = 0;
    virtual std::error_condition create_group(std::string const& group_id, std::string const& group_type) = 0;
    virtual std::error_condition join_group(std::string const& group_id, std::string const& group_type,
                                            std::shared_ptr< mesg_state_mgr > smgr) = 0;

    // Send a client request to the cluster
    virtual bool add_member(std::string const& group_id, std::string const& server_id) = 0;
    virtual bool add_member(std::string const& group_id, std::string const& server_id,
                            bool const wait_for_completion) = 0;
    virtual bool rem_member(std::string const& group_id, std::string const& server_id) = 0;
    virtual void leave_group(std::string const& group_id) = 0;
    virtual bool request_leadership(std::string const& group_id) = 0;
    virtual std::error_condition client_request(std::string const& group_id,
                                                std::shared_ptr< nuraft::buffer >& buf) = 0;
    virtual void get_peers(std::string const& group_id, std::list< std::string >&) const = 0;
    virtual uint32_t logstore_id(std::string const& group_id) const = 0;
    virtual int32_t server_id() const = 0;
    virtual void restart_server() = 0;

    // data channel APIs
    virtual bool bind_data_service_request(std::string const& request_name, std::string const& group_id,
                                           data_service_request_handler_t const& request_handler) = 0;
};

} // namespace nuraft_mesg
