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

// Brief:
//   Defines a simple RAFT-over-gRPC client for the service that is defined
// in raft_service.proto and can be used directly for simple RAFT-to-RAFT
// transports that do not need to expand upon the messages or RPC itself.
//
#pragma once

#include "grpc_client.hpp"
#include "proto/raft_service.grpc.pb.h"

namespace nuraft_mesg {

class simple_grpc_client : public grpc_client< RaftSvc > {
public:
    using grpc_client< RaftSvc >::grpc_client;

protected:
    void send(RaftMessage const& message, handle_resp complete) override {
        _stub->call_unary< RaftMessage, RaftMessage >(message, &RaftSvc::StubInterface::AsyncStep, complete,
                                                      2 /*second-deadline*/);
    }
};

} // namespace nuraft_mesg
