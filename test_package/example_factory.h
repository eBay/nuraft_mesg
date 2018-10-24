#pragma once

#include "example_service.grpc.pb.h"

namespace cornerstone {

struct example_client :
      public grpc_client {
   explicit example_client(std::shared_ptr<::grpc::ChannelInterface> channel) :
         stub_(raft_core::ExampleSvc::NewStub(channel)) {}

   ::grpc::Status send(::grpc::ClientContext *ctx,
                       raft_core::RaftMessage const &message,
                       raft_core::RaftMessage *response) override {
      return stub_->Step(ctx, message, response);
   }

 private:
   std::unique_ptr<typename raft_core::ExampleSvc::Stub> stub_;
};

struct example_factory :
      public rpc_client_factory {
   ptr<rpc_client> create_client(const std::string &endpoint) override {
      auto client = format(fmt("127.0.0.1:900{}"), endpoint);
      return std::make_shared<example_client>(::grpc::CreateChannel(client, grpc::InsecureChannelCredentials()));
   }
};

}
