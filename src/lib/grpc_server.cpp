///
// Copyright 2018 (c) eBay Corporation
//
// Authors:
//      Brian Szmyd <bszmyd@ebay.com>
//
// Brief:
//   grpc_server step function and response transformations
//

#include "grpc_server.hpp"
#include "utils.hpp"

namespace sds {

static
RCResponse*
fromRCResponse(nuraft::resp_msg& rcmsg) {
   auto req = new RCResponse;
   req->set_next_index(rcmsg.get_next_idx());
   req->set_accepted(rcmsg.get_accepted());
   req->set_result_code((ResultCode)(0 - rcmsg.get_result_code()));
   auto ctx = rcmsg.get_ctx();
   if (ctx) {
      req->set_context(ctx->data(), ctx->container_size());
   }
   return req;
}


static
shared<nuraft::req_msg>
toRequest(RaftMessage const& raft_msg) {
    assert(raft_msg.has_rc_request());
    auto const& base = raft_msg.base();
    auto const& req = raft_msg.rc_request();
    auto message = std::make_shared<nuraft::req_msg>(base.term(),
            (nuraft::msg_type)base.type(),
            base.src(),
            base.dest(),
            req.last_log_term(),
            req.last_log_index(),
            req.commit_index());
    auto &log_entries = message->log_entries();
    for (auto const& log : req.log_entries()) {
        auto log_buffer = nuraft::buffer::alloc(log.buffer().size());
        memcpy(log_buffer->data(), log.buffer().data(), log.buffer().size());
        log_entries.push_back(std::make_shared<nuraft::log_entry>(log.term(),
                    log_buffer,
                    (nuraft::log_val_type)log.type()));
    }
    return message;
}

::grpc::Status
grpc_server::step(RaftMessage& request, RaftMessage& reply) {
    LOGTRACEMOD(nuraft, "Stepping [{}] from: [{}] to: [{}]",
            nuraft::msg_type_to_string(nuraft::msg_type(request.base().type())),
            request.base().src(),
            request.base().dest()
            );
    auto rcreq = toRequest(request);
    auto resp = _raft_server->process_req(*rcreq);
    if (!resp) {
        return ::grpc::Status(::grpc::StatusCode::CANCELLED, "Server rejected request");
    }
    assert(resp);
    reply.set_allocated_base(fromBaseRequest(*resp));
    reply.set_allocated_rc_response(fromRCResponse(*resp));
    if (!resp->get_accepted()) {
        auto const srv_conf = _raft_server->get_srv_config(reply.base().dest());
        if (srv_conf) {
            reply.mutable_rc_response()->set_dest_addr(srv_conf->get_endpoint());
        }
    }
    return ::grpc::Status();
}
}
