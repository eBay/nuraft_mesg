/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/

// Brief:
//   grpc_server step function and response transformations
//
#include "nuraft_mesg/grpc_server.hpp"
#include "utils.hpp"
#include "raft_types.pb.h"

namespace nuraft_mesg {

static RCResponse* fromRCResponse(nuraft::resp_msg& rcmsg) {
    auto req = new RCResponse;
    req->set_next_index(rcmsg.get_next_idx());
    req->set_accepted(rcmsg.get_accepted());
    req->set_result_code((ResultCode)(0 - rcmsg.get_result_code()));
    auto ctx = rcmsg.get_ctx();
    if (ctx) { req->set_context(ctx->data(), ctx->container_size()); }
    return req;
}

static std::shared_ptr< nuraft::req_msg > toRequest(RaftMessage const& raft_msg) {
    assert(raft_msg.has_rc_request());
    auto const& base = raft_msg.base();
    auto const& req = raft_msg.rc_request();
    auto message =
        std::make_shared< nuraft::req_msg >(base.term(), (nuraft::msg_type)base.type(), base.src(), base.dest(),
                                            req.last_log_term(), req.last_log_index(), req.commit_index());
    auto& log_entries = message->log_entries();
    for (auto const& log : req.log_entries()) {
        auto log_buffer = nuraft::buffer::alloc(log.buffer().size());
        memcpy(log_buffer->data(), log.buffer().data(), log.buffer().size());
        log_entries.push_back(std::make_shared< nuraft::log_entry >(log.term(), log_buffer,
                                                                    (nuraft::log_val_type)log.type(), log.timestamp()));
    }
    return message;
}

nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > grpc_server::add_srv(const nuraft::srv_config& cfg) {
    return _raft_server->add_srv(cfg);
}

void grpc_server::yield_leadership(bool immediate) { return _raft_server->yield_leadership(immediate, -1); }

bool grpc_server::request_leadership() { return _raft_server->request_leadership(); }

void grpc_server::get_srv_config_all(std::vector< nuraft::ptr< nuraft::srv_config > >& configs_out) {
    _raft_server->get_srv_config_all(configs_out);
}

nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > > grpc_server::rem_srv(int const member_id) {
    return _raft_server->remove_srv(member_id);
}

nuraft::ptr< nuraft::cmd_result< nuraft::ptr< nuraft::buffer > > >
grpc_server::append_entries(std::vector< nuraft::ptr< nuraft::buffer > > const& logs) {
    return _raft_server->append_entries(logs);
}

::grpc::Status grpc_server::step(const RaftMessage& request, RaftMessage& reply) {
    LOGT("Stepping [{}] from: [{}] to: [{}]", nuraft::msg_type_to_string(nuraft::msg_type(request.base().type())),
         request.base().src(), request.base().dest());
    auto rcreq = toRequest(request);
    auto resp = nuraft::raft_server_handler::process_req(_raft_server.get(), *rcreq);
    if (!resp) { return ::grpc::Status(::grpc::StatusCode::CANCELLED, "Server rejected request"); }
    assert(resp);
    reply.set_allocated_base(fromBaseRequest(*resp));
    reply.set_allocated_rc_response(fromRCResponse(*resp));
    if (!resp->get_accepted()) {
        auto const srv_conf = _raft_server->get_srv_config(reply.base().dest());
        if (srv_conf) { reply.mutable_rc_response()->set_dest_addr(srv_conf->get_endpoint()); }
    }
    return ::grpc::Status();
}
} // namespace nuraft_mesg
