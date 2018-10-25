//
// Copyright eBay 2018
// Author: Brian Szmyd <bszmyd@ebay.com>
//

#pragma once

#include <cornerstone.hxx>
#include "raft_types.pb.h"
#include <grpcpp/create_channel.h>
#include <grpcpp/server_builder.h>

namespace cornerstone {

inline
raft_core::RCMsgBase*
fromBaseRequest(msg_base const& rcbase) {
   auto base = new raft_core::RCMsgBase;
   base->set_term(rcbase.get_term());
   base->set_src(rcbase.get_src());
   base->set_dest(rcbase.get_dst());
   base->set_type(rcbase.get_type());
   return base;
}

inline
raft_core::LogEntry*
fromLogEntry(log_entry const& log_entry,
             raft_core::LogEntry* log) {
   log->set_term(log_entry.get_term());
   log->set_type((raft_core::LogType)log_entry.get_val_type());
   auto& buffer = log_entry.get_buf();
   buffer.pos(0);
   log->set_buffer(buffer.data(), buffer.size());
   return log;
}

inline
raft_core::RCRequest*
fromRCRequest(req_msg& rcmsg) {
   auto req = new raft_core::RCRequest;
   req->set_last_log_term(rcmsg.get_last_log_term());
   req->set_last_log_index(rcmsg.get_last_log_idx());
   req->set_commit_index(rcmsg.get_commit_idx());
   for (auto& log_entry : rcmsg.log_entries()) {
      auto entry = req->add_log_entries();
      fromLogEntry(*log_entry, entry);
   }
   return req;
}

inline
raft_core::RCResponse*
fromRCResponse(resp_msg& rcmsg) {
   auto req = new raft_core::RCResponse;
   req->set_next_index(rcmsg.get_next_idx());
   req->set_accepted(rcmsg.get_accepted());
   auto ctx = rcmsg.get_ctx();
   if (ctx) {
      req->set_context(ctx->data(), ctx->length());
   }
   return req;
}

inline
raft_core::RaftMessage
fromRequest(req_msg& rcreq) {
   raft_core::RaftMessage message;
   message.set_allocated_base(fromBaseRequest(rcreq));
   message.set_allocated_rc_request(fromRCRequest(rcreq));
   return message;
}

inline
raft_core::RaftMessage
fromResponse(resp_msg& rcresp) {
   raft_core::RaftMessage message;
   message.set_allocated_base(fromBaseRequest(rcresp));
   message.set_allocated_rc_response(fromRCResponse(rcresp));
   return message;
}

inline
ptr<req_msg>
toRequest(raft_core::RaftMessage const& raft_msg) {
   assert(raft_msg.has_rc_request());
   auto const& base = raft_msg.base();
   auto const& req = raft_msg.rc_request();
   auto message = std::make_shared<req_msg>(base.term(),
                                            (msg_type)base.type(),
                                            base.src(),
                                            base.dest(),
                                            req.last_log_term(),
                                            req.last_log_index(),
                                            req.commit_index());
   auto &log_entries = message->log_entries();
   for (auto const& log : req.log_entries()) {
      auto log_buffer = buffer::alloc(log.buffer().size());
      memcpy(log_buffer->data(), log.buffer().data(), log.buffer().size());
      log_entries.push_back(std::make_shared<log_entry>(log.term(), log_buffer, (log_val_type)log.type()));
   }
   return message;
}

inline
ptr<resp_msg>
toResponse(raft_core::RaftMessage const& raft_msg) {
   assert(raft_msg.has_rc_response());
   auto const& base = raft_msg.base();
   auto const& resp = raft_msg.rc_response();
   auto message = std::make_shared<resp_msg>(base.term(),
                                             (msg_type)base.type(),
                                             base.src(),
                                             base.dest(),
                                             resp.next_index(),
                                             resp.accepted());
   if (0 < resp.context().length()) {
      auto ctx_buffer = buffer::alloc(resp.context().length());
      memcpy(ctx_buffer->data(), resp.context().data(), resp.context().length());
      message->set_ctx(ctx_buffer);
   }
   return message;
}

struct grpc_client : public rpc_client {
   virtual ::grpc::Status send(::grpc::ClientContext* ctx,
                               raft_core::RaftMessage const& message,
                               raft_core::RaftMessage* response) = 0;

   void send(ptr<req_msg>& req, rpc_handler& complete) override {
      ptr<rpc_exception> err;
      ptr<resp_msg> resp;

      ::grpc::ClientContext context;
      raft_core::RaftMessage response;
      auto status = send(&context, fromRequest(*req), &response);

      if (status.ok()) {
         resp = toResponse(response);
      } else {
         err = std::make_shared<rpc_exception>(status.error_message(), req);
      }
      complete(resp, err);
   }
};

struct grpc_service {
   explicit grpc_service(ptr<raft_server>&& raft_server) :
      _raft_server(std::move(raft_server))
   { }

   ~grpc_service() { if (_raft_server) _raft_server->shutdown(); }

   ::grpc::Status step(::grpc::ServerContext *context,
                       ::raft_core::RaftMessage const *request,
                       ::raft_core::RaftMessage *response) {
      auto rcreq = toRequest(*request);
      auto resp = _raft_server->process_req(*rcreq);
      assert(resp);
      response->CopyFrom(fromResponse(*resp));
      return ::grpc::Status();
   }

 private:
   ptr<raft_server> _raft_server;
};

}
