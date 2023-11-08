#pragma once

#include <sisl/logging/logging.h>
#include <grpcpp/generic/async_generic_service.h>

#include "nuraft_mesg/common.hpp"

#define LOGT(...) LOGTRACEMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGD(...) LOGDEBUGMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGI(...) LOGINFOMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGW(...) LOGWARNMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGE(...) LOGERRORMOD(nuraft_mesg, ##__VA_ARGS__)
#define LOGC(...) LOGCRITICALMOD(nuraft_mesg, ##__VA_ARGS__)

namespace nuraft_mesg {

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

} // namespace nuraft_mesg
