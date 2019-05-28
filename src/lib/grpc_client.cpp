///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   grpc_client does the protobuf transformations on nupillar req's
//

#include "grpc_client.hpp"
#include "utils.hpp"

namespace sds {

inline
LogEntry*
fromLogEntry(nupillar::log_entry const& entry, LogEntry* log) {
   log->set_term(entry.get_term());
   log->set_type((LogType)entry.get_val_type());
   auto& buffer = entry.get_buf();
   buffer.pos(0);
   log->set_buffer(buffer.data(), buffer.size());
   return log;
}

inline
RCRequest*
fromRCRequest(nupillar::req_msg& rcmsg) {
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
shared<nupillar::resp_msg>
toResponse(RaftMessage const& raft_msg) {
   if (!raft_msg.has_rc_response()) return nullptr;
   auto const& base = raft_msg.base();
   auto const& resp = raft_msg.rc_response();
   auto message = std::make_shared<grpc_resp>(base.term(),
                                              (nupillar::msg_type)base.type(),
                                              base.src(),
                                              base.dest(),
                                              resp.next_index(),
                                              resp.accepted());
   if (!resp.accepted()) {
       message->dest_addr = resp.dest_addr();
   }
   if (0 < resp.context().length()) {
      auto ctx_buffer = nupillar::buffer::alloc(resp.context().length());
      memcpy(ctx_buffer->data(), resp.context().data(), resp.context().length());
      message->set_ctx(ctx_buffer);
   }
   return message;
}

void
grpc_base_client::send(shared<nupillar::req_msg>& req, nupillar::rpc_handler& complete) {
    assert(req && complete);
    RaftMessage grpc_request;
    grpc_request.set_allocated_base(fromBaseRequest(*req));
    grpc_request.set_allocated_rc_request(fromRCRequest(*req));

    LOGTRACEMOD(nupillar, "Sending [{}] from: [{}] to: [{}]",
            nupillar::msg_type_to_string(nupillar::msg_type(grpc_request.base().type())),
            grpc_request.base().src(),
            grpc_request.base().dest()
            );

    send(grpc_request,
        [req, complete]
        (RaftMessage& response, ::grpc::Status& status) mutable -> void {
            shared<nupillar::rpc_exception> err;
            shared<nupillar::resp_msg> resp;

            if (status.ok()) {
                resp = toResponse(response);
                if (!resp) {
                    err = std::make_shared<nupillar::rpc_exception>("missing response", req);
                }
            } else {
                err = std::make_shared<nupillar::rpc_exception>(status.error_message(), req);
            }
            complete(resp, err);
        });
}

}
