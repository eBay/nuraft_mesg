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

#include "common.hpp"
#include <sisl/fds/buffer.hpp>

namespace nuraft_mesg {

inline RCMsgBase* fromBaseRequest(nuraft::msg_base const& rcbase) {
    auto base = new RCMsgBase;
    base->set_term(rcbase.get_term());
    base->set_src(rcbase.get_src());
    base->set_dest(rcbase.get_dst());
    base->set_type(rcbase.get_type());
    return base;
}

static void serializeToByteBuffer(grpc::ByteBuffer& cli_byte_buf, std::vector< sisl::io_blob > const& cli_buf) {
    std::vector< grpc::Slice > slices;
    slices.reserve(cli_buf.size());
    for (auto const& blob : cli_buf) {
        cli_byte_buf.Clear();
        slices.emplace_back(blob.bytes, grpc::Slice::STATIC_SLICE);
    }
    grpc::ByteBuffer tmp(slices.data(), cli_buf.size());
    cli_byte_buf.Swap(&tmp);
}

static void deserializeFromByteBuffer(grpc::ByteBuffer const& cli_byte_buf, std::vector< sisl::io_blob >& cli_buf) {
    std::vector< grpc::Slice > slices;
    cli_byte_buf.Dump(&slices);
    cli_buf.clear();
    cli_buf.reserve(cli_byte_buf.Length());
    for (auto const& slice : slices) {
        cli_buf.emplace_back(const_cast< uint8_t* >(slice.begin()), slice.size(), false);
    }
}

} // namespace nuraft_mesg
