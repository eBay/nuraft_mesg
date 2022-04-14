///
// Copyright 2018 (c) eBay Corporation
//

#pragma once

#include <condition_variable>
#include <map>
#include <shared_mutex>
#include <nuraft_grpc/grpc_server.hpp>
#include <grpc_helper/rpc_server.hpp>
#include <metrics/metrics.hpp>

#include "messaging_service.grpc.pb.h"

namespace sds::messaging {

template < typename T >
using boxed = std::unique_ptr< T >;

template < typename T >
using shared = std::shared_ptr< T >;

using group_name_t = std::string;
using group_type_t = std::string;

class msg_service;

using lock_type = std::shared_mutex;

class group_metrics : public sisl::MetricsGroupWrapper {
public:
    explicit group_metrics(group_name_t const& group_name) :
            sisl::MetricsGroupWrapper("RAFTGroup", group_name.c_str()) {
        REGISTER_COUNTER(group_steps, "Total group messages received", "raft_group", {"op", "step"});
        REGISTER_COUNTER(group_sends, "Total group messages sent", "raft_group", {"op", "send"});
        register_me_to_farm();
    }

    ~group_metrics() { deregister_me_from_farm(); }
};

using get_server_ctx_cb = std::function< std::error_condition(int32_t srv_id, group_name_t const&, group_type_t const&,
                                                              nuraft::context*& ctx_out,
                                                              shared< group_metrics > metrics, msg_service* sds_msg) >;

struct grpc_server_wrapper {
    explicit grpc_server_wrapper(group_name_t const& group_name);

    shared< nuraft_grpc::grpc_server > m_server;
    shared< group_metrics > m_metrics;
};

class msg_service : public std::enable_shared_from_this< msg_service > {
    get_server_ctx_cb _get_server_ctx;
    std::mutex _raft_sync_lock;
    std::condition_variable_any _raft_servers_sync;
    lock_type _raft_servers_lock;
    std::map< group_name_t, grpc_server_wrapper > _raft_servers;
    std::string const _service_address;
    std::string _default_group_type;

    msg_service(get_server_ctx_cb get_server_ctx, std::string const& service_address) :
            _get_server_ctx(get_server_ctx), _service_address(service_address) {}
    ~msg_service();

public:
    static shared< msg_service > create(get_server_ctx_cb get_server_ctx, std::string const& service_address);

    msg_service(msg_service const&) = delete;
    msg_service& operator=(msg_service const&) = delete;

    void shutdown();

    nuraft::cmd_result_code add_srv(group_name_t const& group_name, nuraft::srv_config const& cfg);
    nuraft::cmd_result_code rm_srv(group_name_t const& group_name, int const member_id);
    bool request_leadership(group_name_t const& group_name);
    void get_srv_config_all(group_name_t const& group_name, std::vector< shared< nuraft::srv_config > >& configs_out);

    nuraft::cmd_result_code append_entries(group_name_t const& group_name,
                                           std::vector< nuraft::ptr< nuraft::buffer > > const& logs);

    void associate(grpc_helper::GrpcServer* server);
    void bind(grpc_helper::GrpcServer* server);

    //::grpc::Status raftStep(RaftGroupMsg& request, RaftGroupMsg& response);
    bool raftStep(const grpc_helper::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data);

    void setDefaultGroupType(std::string const& _type);

    std::error_condition createRaftGroup(int const srv_id, group_name_t const& group_name,
                                         group_type_t const& group_type) {
        return joinRaftGroup(srv_id, group_name, group_type);
    }

    std::error_condition joinRaftGroup(int32_t srv_id, group_name_t const& group_name, group_type_t const&);

    void partRaftGroup(group_name_t const& group_name);

    // Internal intent only
    void shutdown_for(group_name_t const&);
};

} // namespace sds::messaging
