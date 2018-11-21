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

#include "raft_types.pb.h"
#include "utils.hpp"

namespace raft_core {

template<typename TSERVICE>
class grpc_client :
    public cstn::rpc_client,
    public sds::grpc::GrpcConnection<TSERVICE>
{
 public:
   using handle_resp = std::function<void(RaftMessage&, ::grpc::Status&)>;

   grpc_client(std::string const& addr,
               uint32_t deadline,
               ::grpc::CompletionQueue* cq,
               std::string const& target_domain,
               std::string const& ssl_cert) :
     cstn::rpc_client(),
     sds::grpc::GrpcConnection<TSERVICE>(addr, deadline, cq, target_domain, ssl_cert)
   { }

   ~grpc_client() override = default;

   void send(shared<cstn::req_msg>& req, cstn::rpc_handler& complete) override {
     auto message = fromRequest(*req);

     LOGTRACEMOD(raft_core, "Sending [{}] from: [{}] to: [{}]",
         cstn::msg_type_to_string(cstn::msg_type(message.base().type())),
         message.base().src(),
         message.base().dest()
         );

     send(message,
          [req, complete]
          (RaftMessage& response, ::grpc::Status& status) mutable -> void
          {
              shared<cstn::rpc_exception> err;
              shared<cstn::resp_msg> resp;

              if (status.ok()) {
                  resp = toResponse(response);
                  if (!resp) {
                    err = std::make_shared<cstn::rpc_exception>("missing response", req);
                  }
              } else {
                  err = std::make_shared<cstn::rpc_exception>(status.error_message(), req);
              }
              complete(resp, err);
          });
   }

 protected:
   virtual void send(RaftMessage const& message, handle_resp complete) = 0;

 private:
   boxed<TSERVICE> _connection;
   boxed<sds::grpc::GrpcClient> _client;
};

}
