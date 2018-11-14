#pragma once

#include <cornerstone/grpc_factory.hpp>
#include <grpcpp/create_channel.h>

struct example_factory : public cornerstone::grpc_factory
{
  using cornerstone::grpc_factory::grpc_factory;

  cornerstone::ptr<cornerstone::rpc_client>
  create_client(const std::string &client) override {
    auto const endpoint = format(fmt("127.0.0.1:900{}"), client);
    LOGDEBUGMOD(raft_core, "Creating client for [{}] @ [{}]", client, endpoint);
    return std::make_shared<cornerstone::simple_grpc_client>(
        ::grpc::CreateChannel(endpoint, ::grpc::InsecureChannelCredentials()));
  }
};
