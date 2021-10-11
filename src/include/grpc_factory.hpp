///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Implements cornerstone's rpc_client_factory providing grpc_helper::GrpcAsyncClient
//   inherited rpc_client instances sharing a common worker pool.

#pragma once

#include <future>
#include <memory>
#include <mutex>

#include "common.hpp"

SDS_LOGGING_DECL(nuraft)

namespace nuraft_grpc {

class grpc_factory : public nuraft::rpc_client_factory, public std::enable_shared_from_this< grpc_factory > {
    std::string _worker_name;
    std::mutex _client_lock;
    std::map< std::string, std::shared_ptr< nuraft::rpc_client > > _clients;

public:
    grpc_factory(int const cli_thread_count, std::string const& name);
    ~grpc_factory() override = default;

    std::string const& workerName() const { return _worker_name; }

    nuraft::ptr< nuraft::rpc_client > create_client(const std::string& client) override;

    virtual std::error_condition create_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >&) = 0;

    virtual std::error_condition reinit_client(const std::string& client, nuraft::ptr< nuraft::rpc_client >&) = 0;

    // Construct and send an AddServer message to the cluster
    std::future< nuraft::cmd_result_code > add_server(uint32_t const srv_id, std::string const& srv_addr,
                                                      nuraft::srv_config const& dest_cfg);

    // Send a client request to the cluster
    std::future< nuraft::cmd_result_code > client_request(shared< nuraft::buffer > buf,
                                                          nuraft::srv_config const& dest_cfg);

    // Construct and send a RemoveServer message to the cluster
    std::future< nuraft::cmd_result_code > rem_server(uint32_t const srv_id, nuraft::srv_config const& dest_cfg);
};

} // namespace nuraft_grpc
