//
// Copyright eBay 2018
//

#pragma once

#include <cornerstone.hxx>
#include "raft_service.grpc.pb.h"
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
   auto const& buffer = log_entry.get_buf();
   log->set_buffer(buffer.data(), buffer.length());
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
raft_core::RaftMessage*
fromRequest(req_msg& rcreq) {
   auto message = new raft_core::RaftMessage;
   message->set_allocated_base(fromBaseRequest(rcreq));
   message->set_allocated_rc_request(fromRCRequest(rcreq));
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
      auto log_buffer = buffer::alloc(log.buffer().length());
      memcpy(log_buffer->data(), log.buffer().data(), log.buffer().length());
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
   auto ctx_buffer = buffer::alloc(resp.context().length());
   memcpy(ctx_buffer->data(), resp.context().data(), resp.context().length());

   message->set_ctx(ctx_buffer);
   return message;
}

struct raft_client final : public rpc_client {
   explicit raft_client(std::shared_ptr<::grpc::ChannelInterface> channel) :
         stub_(raft_core::RaftMemberSvc::NewStub(channel))
   {
   }

   void send(ptr<req_msg>& req, rpc_handler& complete) override {
      ptr<rpc_exception> err;
      ptr<resp_msg> resp;

      ::grpc::ClientContext context;
      raft_core::StepRequest step_request;
      step_request.set_allocated_msg(fromRequest(*req));
      raft_core::RaftMessage response;
      auto status = stub_->Step(&context, step_request, &response);

      if (status.ok()) {
         resp = toResponse(response);
      } else {
         err = std::make_shared<rpc_exception>(status.error_message(), req);
      }
      complete(resp, err);
   }

 private:
   std::unique_ptr<raft_core::RaftMemberSvc::Stub> stub_;
};

struct grpc_service :
      public rpc_client_factory,
      public raft_core::RaftMemberSvc::Service
{
   ::grpc::Status Step(::grpc::ServerContext *context,
                       ::raft_core::StepRequest const *request,
                       ::raft_core::RaftMessage *response) override;

   ptr<rpc_client> create_client(const std::string &endpoint) override {
      return std::make_shared<raft_client>(::grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials()));
   }

   void registerRaftCore(ptr<raft_server> raft_server) {
      _raft_server = raft_server;
   }

 private:
   ptr<raft_server> _raft_server;
};

::grpc::Status
grpc_service::Step(::grpc::ServerContext *,
                   ::raft_core::StepRequest const *request,
                   ::raft_core::RaftMessage *response) {
   auto rcreq = toRequest(request->msg());
   auto resp = _raft_server->process_req(*rcreq);
   assert(resp);
   response->CopyFrom(fromResponse(*resp));
   return ::grpc::Status();
}

}
