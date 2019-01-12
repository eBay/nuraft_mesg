///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Implements cornerstone's rpc_client::send(...) routine to translate
// and execute the call over gRPC asynchrously.
//

#pragma once

#include <cornerstone.hxx>
#include <sds_grpc/client.h>

#include "common.hpp"

namespace raft_core {

class grpc_resp : public cstn::resp_msg
{
 public:
    using cstn::resp_msg::resp_msg;
    ~grpc_resp() override = default;

    std::string dest_addr;
};

class grpc_base_client : public cstn::rpc_client
{
 public:
    using handle_resp = std::function<void(RaftMessage&, ::grpc::Status&)>;

    using cstn::rpc_client::rpc_client;
    ~grpc_base_client() override = default;

    void send(shared<cstn::req_msg>& req, cstn::rpc_handler& complete) override;

 protected:
    virtual void send(RaftMessage const& message, handle_resp complete) = 0;
};

template<typename TSERVICE>
class grpc_client :
    public grpc_base_client,
    public sds::grpc::GrpcAsyncClient
{
 public:
    grpc_client(std::string const& worker_name,
                std::string const& addr,
                std::string const& target_domain = "",
                std::string const& ssl_cert = "") :
        grpc_base_client(),
        sds::grpc::GrpcAsyncClient(addr, target_domain, ssl_cert),
        _worker_name(worker_name.data())
    { }

    ~grpc_client() override = default;

    bool init() override {
        // Re-create channel only if current channel is busted.
        if (!_stub || !is_connection_ready()) {
            if (!sds::grpc::GrpcAsyncClient::init()) {
                LOGERROR("Initializing client failed!");
                return false;
            }
            _stub = sds::grpc::GrpcAsyncClient::make_stub<TSERVICE>(_worker_name);
        } else {
            LOGDEBUGMOD(raft_core, "Channel looks fine, re-using");
        }
        return (!!_stub);
    }

 protected:
    char const* _worker_name;
    typename ::sds::grpc::GrpcAsyncClient::AsyncStub<TSERVICE>::UPtr _stub;
};

}
