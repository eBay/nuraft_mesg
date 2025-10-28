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

#include <sisl/fds/buffer.hpp>

// Brief:
//   grpc_client does the protobuf transformations on nuraft req's
//
#include "flatb_client.hpp"
#include "utils.hpp"
#include "fbschemas/raft_types_generated.h"

namespace nuraft_mesg {

inline auto fromRequest(nuraft::req_msg& rcmsg) {
    flatbuffers::FlatBufferBuilder builder(1024);
    auto msg_base = MessageBase{rcmsg.get_term(), rcmsg.get_type(), rcmsg.get_src(), rcmsg.get_dst()};
    auto entry_vec = std::vector< flatbuffers::Offset< LogEntry > >();
    for (auto const& entry : rcmsg.log_entries()) {
        auto& buffer = entry->get_buf();
        buffer.pos(0);
        auto log_buf = builder.CreateVector(buffer.data(), buffer.size());
        entry_vec.push_back(CreateLogEntry(builder, entry->get_term(), (LogType)entry->get_val_type(), log_buf,
                                           entry->get_timestamp()));
    }
    auto log_entries = builder.CreateVector(entry_vec);
    auto req = CreateRequest(builder, &msg_base, rcmsg.get_last_log_term(), rcmsg.get_last_log_idx(),
                             rcmsg.get_commit_idx(), log_entries);
    return req;
}

inline std::shared_ptr< nuraft::resp_msg > toResponse(Response const& resp) {
    auto const& base = *resp.msg_base();
    auto message = std::make_shared< grpc_resp >(base.term(), (nuraft::msg_type)base.type(), base.src(), base.dest(),
                                                 resp.next_index(), resp.accepted());
    message->set_result_code((nuraft::cmd_result_code)(resp.result_code()));
    if (nuraft::cmd_result_code::NOT_LEADER == message->get_result_code()) {
        LOGI("Leader has changed!");
        message->dest_addr = resp.dest_addr()->str();
    }
    if (0 < resp.context()->size()) {
        auto ctx_buffer = nuraft::buffer::alloc(resp.context()->size());
        memcpy(ctx_buffer->data(), resp.context()->data(), resp.context()->size());
        message->set_ctx(ctx_buffer);
    }
    return message;
}

std::atomic_uint64_t grpc_base_client::_client_counter = 0ul;

void grpc_base_client::send(std::shared_ptr< nuraft::req_msg >& req, nuraft::rpc_handler& complete, uint64_t) {
    assert(req && complete);
    static_cast< grpc_flatb_client* >(this)->send_fb(
        fromRequest(*req), [req, complete](Response& response, ::grpc::Status& status) mutable -> void {
            std::shared_ptr< nuraft::rpc_exception > err;
            std::shared_ptr< nuraft::resp_msg > resp;

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
