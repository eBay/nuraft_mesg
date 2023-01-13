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
//   grpc_client does the protobuf transformations on nuraft req's
//
#include "grpc_client.hpp"
#include "utils.hpp"

namespace nuraft_mesg {

inline LogEntry* fromLogEntry(nuraft::log_entry const& entry, LogEntry* log) {
    log->set_term(entry.get_term());
    log->set_type((LogType)entry.get_val_type());
    auto& buffer = entry.get_buf();
    buffer.pos(0);
    log->set_buffer(buffer.data(), buffer.size());
    log->set_timestamp(entry.get_timestamp());
    return log;
}

inline RCRequest* fromRCRequest(nuraft::req_msg& rcmsg) {
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

inline shared< nuraft::resp_msg > toResponse(RaftMessage const& raft_msg) {
    if (!raft_msg.has_rc_response()) return nullptr;
    auto const& base = raft_msg.base();
    auto const& resp = raft_msg.rc_response();
    auto message = std::make_shared< grpc_resp >(base.term(), (nuraft::msg_type)base.type(), base.src(), base.dest(),
                                                 resp.next_index(), resp.accepted());
    message->set_result_code((nuraft::cmd_result_code)(0 - resp.result_code()));
    if (nuraft::cmd_result_code::NOT_LEADER == message->get_result_code()) {
        LOGINFOMOD(nuraft_mesg, "Leader has changed!");
        message->dest_addr = resp.dest_addr();
    }
    if (0 < resp.context().length()) {
        auto ctx_buffer = nuraft::buffer::alloc(resp.context().length());
        memcpy(ctx_buffer->data(), resp.context().data(), resp.context().length());
        message->set_ctx(ctx_buffer);
    }
    return message;
}

std::atomic_uint64_t grpc_base_client::_client_counter = 0ul;

void grpc_base_client::send(shared< nuraft::req_msg >& req, nuraft::rpc_handler& complete, uint64_t timeout_ms) {
    assert(req && complete);
    RaftMessage grpc_request;
    grpc_request.set_allocated_base(fromBaseRequest(*req));
    grpc_request.set_allocated_rc_request(fromRCRequest(*req));

    LOGTRACEMOD(nuraft_mesg, "Sending [{}] from: [{}] to: [{}]",
                nuraft::msg_type_to_string(nuraft::msg_type(grpc_request.base().type())), grpc_request.base().src(),
                grpc_request.base().dest());

    send(grpc_request, [req, complete](RaftMessage& response, ::grpc::Status& status) mutable -> void {
        shared< nuraft::rpc_exception > err;
        shared< nuraft::resp_msg > resp;

        if (status.ok()) {
            resp = toResponse(response);
            if (!resp) { err = std::make_shared< nuraft::rpc_exception >("missing response", req); }
        } else {
            err = std::make_shared< nuraft::rpc_exception >(status.error_message(), req);
        }
        complete(resp, err);
    });
}

} // namespace nuraft_mesg
