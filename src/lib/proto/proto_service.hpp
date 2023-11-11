///
// Copyright 2018 (c) eBay Corporation
//

#pragma once

#include "lib/service.hpp"

namespace nuraft_mesg {

class RaftGroupMsg;
class RaftMessage;
class Messaging;

class proto_service : public msg_service {
public:
    using msg_service::msg_service;
    void associate(sisl::GrpcServer* server);
    void bind(sisl::GrpcServer* server);

    bool raftStep(const sisl::AsyncRpcDataPtr< Messaging, RaftGroupMsg, RaftGroupMsg >& rpc_data);
    ::grpc::Status step(nuraft::raft_server& server, const RaftMessage& request, RaftMessage& reply);
};

} // namespace nuraft_mesg
