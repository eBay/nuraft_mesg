///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//

#pragma once

#include <sds_logging/logging.h>
#include <raft_core_grpc/grpc_factory.hpp>
#include <raft_core_grpc/simple_client.hpp>

struct example_factory : public raft_core::grpc_factory
{
  using raft_core::grpc_factory::grpc_factory;

  std::error_condition
  create_client(const std::string &client,
                raft_core::shared<cornerstone::rpc_client>& raft_client) override {
    auto const endpoint = format(FMT_STRING("127.0.0.1:900{}"), client);
    LOGDEBUGMOD(raft_core, "Creating client for [{}] @ [{}]", client, endpoint);

    raft_client = sds::grpc::GrpcAsyncClient::make<raft_core::simple_grpc_client>(endpoint, "", "", 2);
    return (!raft_client) ? std::make_error_condition(std::errc::connection_aborted) :
                            std::error_condition();
  }
};
