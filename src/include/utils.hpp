///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   Common translation routines, aliases and types.
//

#pragma once

#include <cornerstone.hxx>
#include <sds_logging/logging.h>
#include "raft_types.pb.h"

SDS_LOGGING_DECL(raft_core)

namespace raft_core {

namespace cstn = cornerstone;

template<typename T>
using boxed = std::unique_ptr<T>;

template<typename T>
using shared = std::shared_ptr<T>;

inline
RCMsgBase*
fromBaseRequest(cstn::msg_base const& rcbase) {
   auto base = new RCMsgBase;
   base->set_term(rcbase.get_term());
   base->set_src(rcbase.get_src());
   base->set_dest(rcbase.get_dst());
   base->set_type(rcbase.get_type());
   return base;
}

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
RCResponse*
fromRCResponse(cstn::resp_msg& rcmsg) {
   auto req = new RCResponse;
   req->set_next_index(rcmsg.get_next_idx());
   req->set_accepted(rcmsg.get_accepted());
   auto ctx = rcmsg.get_ctx();
   if (ctx) {
      req->set_context(ctx->data(), ctx->length());
   }
   return req;
}

inline
RaftMessage
fromRequest(cstn::req_msg& rcreq) {
   RaftMessage message;
   message.set_allocated_base(fromBaseRequest(rcreq));
   message.set_allocated_rc_request(fromRCRequest(rcreq));
   return message;
}

inline
RaftMessage
fromResponse(cstn::resp_msg& rcresp) {
   RaftMessage message;
   message.set_allocated_base(fromBaseRequest(rcresp));
   message.set_allocated_rc_response(fromRCResponse(rcresp));
   return message;
}

inline
shared<cstn::req_msg>
toRequest(RaftMessage const& raft_msg) {
   assert(raft_msg.has_rc_request());
   auto const& base = raft_msg.base();
   auto const& req = raft_msg.rc_request();
   auto message = std::make_shared<cstn::req_msg>(base.term(),
                                                  (cstn::msg_type)base.type(),
                                                  base.src(),
                                                  base.dest(),
                                                  req.last_log_term(),
                                                  req.last_log_index(),
                                                  req.commit_index());
   auto &log_entries = message->log_entries();
   for (auto const& log : req.log_entries()) {
      auto log_buffer = cstn::buffer::alloc(log.buffer().size());
      memcpy(log_buffer->data(), log.buffer().data(), log.buffer().size());
      log_entries.push_back(std::make_shared<cstn::log_entry>(log.term(),
                                                              log_buffer,
                                                              (cstn::log_val_type)log.type()));
   }
   return message;
}

inline
shared<cstn::resp_msg>
toResponse(RaftMessage const& raft_msg) {
   assert(raft_msg.has_rc_response());
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

}
