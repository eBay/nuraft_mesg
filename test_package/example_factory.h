///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//

#pragma once

#include <string>

#include <sds_logging/logging.h>
#include <nuraft_grpc/grpc_factory.hpp>
#include <nuraft_grpc/simple_client.hpp>

struct example_factory : public sds::grpc_factory {
    using sds::grpc_factory::grpc_factory;

    std::error_condition create_client(const std::string&                 client,
                                       sds::shared< nuraft::rpc_client >& raft_client) override {
        LOGDEBUGMOD(nuraft, "Creating client for [{}]", client);
        auto const endpoint = format(FMT_STRING("127.0.0.1:{}"), 9000 + std::stol(client));
        LOGDEBUGMOD(nuraft, "Creating client for [{}] @ [{}]", client, endpoint);

        raft_client = sds::grpc::GrpcAsyncClient::make< sds::simple_grpc_client >(workerName(), endpoint);
        return (!raft_client) ? std::make_error_condition(std::errc::connection_aborted) : std::error_condition();
    }

    std::error_condition reinit_client(const std::string&                 client,
                                       sds::shared< nuraft::rpc_client >& raft_client) override {
        assert(raft_client);
        auto grpc_client = std::dynamic_pointer_cast< sds::simple_grpc_client >(raft_client);
        return (!grpc_client->is_connection_ready()) ? create_client(client, raft_client) : std::error_condition();
    }
};
