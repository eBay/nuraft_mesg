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

inline
LogEntry*
fromLogEntry(cstn::log_entry const& entry, LogEntry* log) {
   log->set_term(entry.get_term());
   log->set_type((LogType)entry.get_val_type());
   auto& buffer = entry.get_buf();
   buffer.pos(0);
   log->set_buffer(buffer.data(), buffer.size());
   return log;
}


inline
RCRequest*
fromRCRequest(cstn::req_msg& rcmsg) {
   auto req = new RCRequest;
   req->set_last_log_term(rcmsg.get_last_log_term());
   req->set_last_log_index(rcmsg.get_last_log_idx());
   req->set_commit_index(rcmsg.get_commit_idx());
   for (auto& rc_entry : rcmsg.log_entries()) {
      auto entry = req->add_log_entries();
      fromLogEntry(*rc_entry, entry);
   }
   return req;
}

inline
shared<cstn::resp_msg>
toResponse(RaftMessage const& raft_msg) {
   if (!raft_msg.has_rc_response()) return nullptr;
   auto const& base = raft_msg.base();
   auto const& resp = raft_msg.rc_response();
   auto message = std::make_shared<cstn::resp_msg>(base.term(),
                                                   (cstn::msg_type)base.type(),
                                                   base.src(),
                                                   base.dest(),
                                                   resp.next_index(),
                                                   resp.accepted());
   if (0 < resp.context().length()) {
      auto ctx_buffer = cstn::buffer::alloc(resp.context().length());
      memcpy(ctx_buffer->data(), resp.context().data(), resp.context().length());
      message->set_ctx(ctx_buffer);
   }
   return message;
}


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
     assert(req && complete);
     RaftMessage grpc_request;
     grpc_request.set_allocated_base(fromBaseRequest(*req));
     grpc_request.set_allocated_rc_request(fromRCRequest(*req));

     LOGTRACEMOD(raft_core, "Sending [{}] from: [{}] to: [{}]",
         cstn::msg_type_to_string(cstn::msg_type(grpc_request.base().type())),
         grpc_request.base().src(),
         grpc_request.base().dest()
         );

     send(grpc_request,
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
