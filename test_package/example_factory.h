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

#include <string>

#include <sisl/logging/logging.h>
#include <nuraft_grpc/grpc_factory.hpp>
#include <nuraft_grpc/simple_client.hpp>

struct example_factory : public nuraft_grpc::grpc_factory {
    using nuraft_grpc::grpc_factory::grpc_factory;

    std::error_condition create_client(const std::string& client,
                                       nuraft_grpc::shared< nuraft::rpc_client >& raft_client) override {
        LOGDEBUGMOD(nuraft, "Creating client for [{}]", client);
        auto const endpoint = format(FMT_STRING("127.0.0.1:{}"), 9000 + std::stol(client));
        LOGDEBUGMOD(nuraft, "Creating client for [{}] @ [{}]", client, endpoint);

        raft_client = sisl::GrpcAsyncClient::make< nuraft_grpc::simple_grpc_client >(workerName(), endpoint);
        return (!raft_client) ? std::make_error_condition(std::errc::connection_aborted) : std::error_condition();
    }

    std::error_condition reinit_client(const std::string& client,
                                       nuraft_grpc::shared< nuraft::rpc_client >& raft_client) override {
        assert(raft_client);
        auto grpc_client = std::dynamic_pointer_cast< nuraft_grpc::simple_grpc_client >(raft_client);
        return (!grpc_client->is_connection_ready()) ? create_client(client, raft_client) : std::error_condition();
    }
};
