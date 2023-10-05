///
// Copyright 2018 (c) eBay Corporation
//

#pragma once

#include <condition_variable>
#include <libnuraft/async.hxx>
#include <map>
#include <folly/SharedMutex.h>
#include <sisl/grpc/rpc_server.hpp>
#include <sisl/metrics/metrics.hpp>

#include "nuraft_mesg/grpc_server.hpp"

#include "proto/messaging_service.grpc.pb.h"
#include "manager_impl.hpp"
#include "data_service_grpc.hpp"

namespace nuraft_mesg {

template < typename T >
using shared = std::shared_ptr< T >;

class msg_service;
class mesg_factory;
class repl_service_ctx_grpc;

using lock_type = folly::SharedMutex;

class group_metrics : public sisl::MetricsGroupWrapper {
public:
    explicit group_metrics(group_id_t const& group_id) :
            sisl::MetricsGroupWrapper("RAFTGroup", to_string(group_id).c_str()) {
        REGISTER_COUNTER(group_steps, "Total group messages received", "raft_group", {"op", "step"});
        REGISTER_COUNTER(group_sends, "Total group messages sent", "raft_group", {"op", "send"});
        register_me_to_farm();
    }

    ~group_metrics() { deregister_me_from_farm(); }
};

using get_server_ctx_cb =
    std::function< nuraft::cmd_result_code(int32_t srv_id, group_id_t const&, group_type_t const&,
                                           nuraft::context*& ctx_out, std::shared_ptr< group_metrics > metrics) >;

// pluggable type for data service
using data_service_t = data_service_grpc;

struct grpc_server_wrapper {
    explicit grpc_server_wrapper(group_id_t const& group_id);

    std::shared_ptr< grpc_server > m_server;
    std::shared_ptr< group_metrics > m_metrics;
};

class msg_service : public std::enable_shared_from_this< msg_service > {
    get_server_ctx_cb _get_server_ctx;
    std::mutex _raft_sync_lock;
    std::condition_variable_any _raft_servers_sync;
    lock_type _raft_servers_lock;
    std::map< group_id_t, grpc_server_wrapper > _raft_servers;
    peer_id_t const _service_address;
    std::string _default_group_type;
    data_service_t _data_service;
    bool _data_service_enabled;

    msg_service(get_server_ctx_cb get_server_ctx, peer_id_t const& service_address, bool const enable_data_service);
    ~msg_service();

public:
    static std::shared_ptr< msg_service > create(get_server_ctx_cb get_server_ctx, peer_id_t const& service_address,
                                                 bool const enable_data_service);

    msg_service(msg_service const&) = delete;
    msg_service& operator=(msg_service const&) = delete;

    void shutdown();

    NullAsyncResult add_srv(group_id_t const& group_id, nuraft::srv_config const& cfg);
    NullAsyncResult append_entries(group_id_t const& group_id,
                                   std::vector< nuraft::ptr< nuraft::buffer > > const& logs);
    NullAsyncResult rm_srv(group_id_t const& group_id, int const member_id);

    bool request_leadership(group_id_t const& group_id);
    void get_srv_config_all(group_id_t const& group_id,
                            std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out);

    void associate(sisl::GrpcServer* server);
    void bind(sisl::GrpcServer* server);
    bool bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                   data_service_request_handler_t const& request_handler);

    //::grpc::Status raftStep(RaftGroupMsg& request, RaftGroupMsg& response);
    bool raftStep(const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data);

    void setDefaultGroupType(std::string const& _type);

    nuraft::cmd_result_code createRaftGroup(int const srv_id, group_id_t const& group_id,
                                            group_type_t const& group_type) {
        return joinRaftGroup(srv_id, group_id, group_type);
    }

    nuraft::cmd_result_code joinRaftGroup(int32_t srv_id, group_id_t const& group_id, group_type_t const&);

    void partRaftGroup(group_id_t const& group_id);

    // Internal intent only
    void shutdown_for(group_id_t const&);
};

} // namespace nuraft_mesg
