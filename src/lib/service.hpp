///
// Copyright 2018 (c) eBay Corporation
//
#pragma once

#include <map>

#include <libnuraft/async.hxx>
#include <folly/concurrency/ConcurrentHashMap.h>
#include <sisl/grpc/rpc_server.hpp>
#include <sisl/metrics/metrics.hpp>

#include "grpc_server.hpp"
#include "manager_impl.hpp"
#include "data_service_grpc.hpp"

/// This is for the ConcurrentHashMap
namespace std {
template <>
struct hash< boost::uuids::uuid > {
    size_t operator()(const boost::uuids::uuid& uid) { return boost::hash< boost::uuids::uuid >()(uid); }
};
} // namespace std

namespace nuraft_mesg {

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

struct grpc_server_wrapper {
    std::shared_ptr< group_metrics > m_metrics;
    std::unique_ptr< grpc_server > m_server;
};

class msg_service : public nuraft::raft_server_handler, public std::enable_shared_from_this< msg_service > {
    bool const _data_service_enabled;
    data_service_grpc _data_service;
    std::string const _default_group_type;
    std::weak_ptr< ManagerImpl > _manager;

protected:
    folly::ConcurrentHashMap< group_id_t, grpc_server_wrapper > _raft_servers;
    peer_id_t const _service_address;

public:
    // Each serialization implementation must provide this static
    static std::shared_ptr< msg_service > create(std::shared_ptr< ManagerImpl > const& manager,
                                                 peer_id_t const& service_address,
                                                 std::string const& default_group_type, bool const enable_data_service);

    msg_service(std::shared_ptr< ManagerImpl > const& manager, peer_id_t const& service_address,
                std::string const& default_group_type, bool const enable_data_service);
    virtual ~msg_service();
    msg_service(msg_service const&) = delete;
    msg_service& operator=(msg_service const&) = delete;

    // Override the following for each serialization implementation
    virtual void associate(sisl::GrpcServer* server);
    virtual void bind(sisl::GrpcServer* server);
    //

    void shutdown();

    NullAsyncResult add_member(group_id_t const& group_id, nuraft::srv_config const& cfg);
    NullAsyncResult rem_member(group_id_t const& group_id, int const member_id);
    bool become_leader(group_id_t const& group_id);
    NullAsyncResult append_entries(group_id_t const& group_id,
                                   std::vector< nuraft::ptr< nuraft::buffer > > const& logs);

    void get_srv_config_all(group_id_t const& group_id,
                            std::vector< std::shared_ptr< nuraft::srv_config > >& configs_out);
    void leave_group(group_id_t const& group_id);

    bool bind_data_service_request(std::string const& request_name, group_id_t const& group_id,
                                   data_service_request_handler_t const& request_handler);

    nuraft::cmd_result_code joinRaftGroup(int32_t srv_id, group_id_t const& group_id, group_type_t const&);

    // Internal intent only
    void shutdown_for(group_id_t const&);
};

} // namespace nuraft_mesg
