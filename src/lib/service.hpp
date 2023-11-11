///
// Copyright 2018 (c) eBay Corporation
//
#pragma once

#include <map>

#include <libnuraft/async.hxx>
#include <folly/SharedMutex.h>
#include <sisl/grpc/rpc_server.hpp>
#include <sisl/metrics/metrics.hpp>

#include "grpc_server.hpp"
#include "manager_impl.hpp"
#include "data_service_grpc.hpp"

namespace nuraft_mesg {

template < typename T >
using shared = std::shared_ptr< T >;

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

// pluggable type for data service
using data_service_t = data_service_grpc;

struct grpc_server_wrapper {
    explicit grpc_server_wrapper(group_id_t const& group_id);

    std::shared_ptr< grpc_server > m_server;
    std::shared_ptr< group_metrics > m_metrics;
};

class msg_service : public std::enable_shared_from_this< msg_service >, public nuraft::raft_server_handler {
    bool _data_service_enabled;
    data_service_t _data_service;
    std::string _default_group_type;
    std::weak_ptr< ManagerImpl > _manager;

protected:
    lock_type _raft_servers_lock;
    std::map< group_id_t, grpc_server_wrapper > _raft_servers;
    peer_id_t const _service_address;

public:
    msg_service(std::shared_ptr< ManagerImpl > const& manager, peer_id_t const& service_address,
                bool const enable_data_service);
    virtual ~msg_service();
    static std::shared_ptr< msg_service > create(std::shared_ptr< ManagerImpl > const& manager,
                                                 peer_id_t const& service_address, bool const enable_data_service);

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

    virtual void associate(sisl::GrpcServer* server);
    virtual void bind(sisl::GrpcServer* server);
    bool bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                   data_service_request_handler_t const& request_handler);

    void setDefaultGroupType(std::string const& _type);

    nuraft::cmd_result_code joinRaftGroup(int32_t srv_id, group_id_t const& group_id, group_type_t const&);

    void partRaftGroup(group_id_t const& group_id);

    // Internal intent only
    void shutdown_for(group_id_t const&);
};

} // namespace nuraft_mesg
