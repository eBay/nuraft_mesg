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
//   Common transformations
//
#pragma once

#include "nuraft_mesg/mesg_factory.hpp"
#include "fbschemas/raft_types_generated.h"

#include "lib/common_lib.hpp"

namespace nuraft_mesg {

// PROTO
/*
inline RCMsgBase* fromBaseRequest(nuraft::msg_base const& rcbase) {
    auto base = new RCMsgBase;
    base->set_term(rcbase.get_term());
    base->set_src(rcbase.get_src());
    base->set_dest(rcbase.get_dst());
    base->set_type(rcbase.get_type());
    return base;
}

[[maybe_unused]] static void serialize_to_byte_buffer(grpc::ByteBuffer& cli_byte_buf, io_blob_list_t const& cli_buf) {
    folly::small_vector< grpc::Slice, 4 > slices;
    for (auto const& blob : cli_buf) {
        slices.emplace_back(blob.bytes, blob.size, grpc::Slice::STATIC_SLICE);
    }
    cli_byte_buf.Clear();
    grpc::ByteBuffer tmp(slices.data(), cli_buf.size());
    cli_byte_buf.Swap(&tmp);
}

[[maybe_unused]] static grpc::Status deserialize_from_byte_buffer(grpc::ByteBuffer const& cli_byte_buf,
                                                                  sisl::io_blob& cli_buf) {
    grpc::Slice slice;
    auto status = cli_byte_buf.TrySingleSlice(&slice);
    if (!status.ok()) { return status; }
    cli_buf.bytes = const_cast< uint8_t* >(slice.begin());
    cli_buf.size = slice.size();
    return status;
}

// generic rpc server looks up rpc name in a map and calls the corresponding callback. To avoid another lookup in this
// layer, we registed one callback for each (group_id, request_name) pair. The rpc_name is their concatenation.
[[maybe_unused]] static std::string get_generic_method_name(std::string const& request_name,
                                                            group_id_t const& group_id) {
    return fmt::format("{}|{}", request_name, group_id);
}
*/

} // namespace nuraft_mesg
