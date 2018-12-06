///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Implements cornerstone's rpc_client_factory providing sds::grpc::GrpcAsyncClient
//   inherited rpc_client instances sharing a common worker pool.

#pragma once

#include <future>
#include <memory>
#include <mutex>

#include "common.hpp"

SDS_LOGGING_DECL(raft_core)

namespace raft_core {

class grpc_factory :
  public cstn::rpc_client_factory,
  public std::enable_shared_from_this<grpc_factory>
{
    mutable std::mutex _leader_lock;
    int32_t            _current_leader;
    std::string        _worker_name;
    std::mutex _client_lock;
    std::map<std::string, std::shared_ptr<cstn::rpc_client>> _clients;

 public:
    grpc_factory(int32_t const current_leader, int const cli_thread_count, std::string const& name);
    ~grpc_factory() override = default;

    std::string const& workerName() const { return _worker_name; }

    int32_t current_leader() const                { std::lock_guard<std::mutex> lk(_leader_lock); return _current_leader; }
    void update_leader(int32_t const leader)      { std::lock_guard<std::mutex> lk(_leader_lock); _current_leader = leader; }

    cstn::ptr<cstn::rpc_client>
    create_client(const std::string &client) override;

    virtual
    std::error_condition
    create_client(const std::string &client, cstn::ptr<cstn::rpc_client>&) = 0;

    virtual
    std::error_condition
    reinit_client(cstn::ptr<cstn::rpc_client>&) = 0;

    // Construct and send an AddServer message to the cluster
    std::future<bool>
    add_server(uint32_t const srv_id);

    // Send a client request to the cluster
    std::future<bool>
    client_request(shared<cstn::buffer> buf);

    // Construct and send a RemoveServer message to the cluster
    std::future<bool>
    rem_server(uint32_t const srv_id);

    // Send a pre-made message to the cluster
    std::future<bool>
    cluster_request(shared<cstn::req_msg> msg);

};

}
