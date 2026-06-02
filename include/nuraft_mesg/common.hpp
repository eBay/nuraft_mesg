#pragma once

#include <expected>
#include <optional>
#include <variant>

#include <boost/container/small_vector.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <libnuraft/async.hxx>
#include <sisl/fds/buffer.hpp>
#include <sisl/logging/logging.h>
#include <sisl/async/task.hpp>

#include "errors.hpp"

SISL_LOGGING_DECL(nuraft_mesg)

#define NURAFTMESG_LOG_MODS nuraft_mesg, grpc_server

namespace sisl {
class GenericClientResponse;
} // namespace sisl

namespace nuraft_mesg {

using peer_id_t = boost::uuids::uuid;
using group_id_t = boost::uuids::uuid;
using group_type_t = std::string;
using svr_id_t = int32_t;
using io_blob_list_t = boost::container::small_vector< sisl::io_blob, 4 >;

template < typename T >
using result = std::expected< T, std::error_condition >;
using null_result = result< void >;

// Map a raw libnuraft result code to a null_result: OK -> success, otherwise the collapsed condition
// (see errors.hpp). The internally-handled codes are resolved before reaching here.
inline null_result to_null_result(nuraft::cmd_result_code code) {
    if (code == nuraft::cmd_result_code::OK) return null_result{};
    return std::unexpected(to_condition(code));
}

// Coroutine-native async result: a co_await-able stdexec sender (composes with sisl::async::when_all). Both
// the data-service and control-plane paths return this -- nothing in the public API returns a std::future.
template < typename T >
using async_task = sisl::async::task< result< T > >;
using null_async_task = async_task< void >;

ENUM(role_regex, uint8_t, LEADER, FOLLOWER, ALL, ANY);
using destination_t = std::variant< peer_id_t, role_regex, svr_id_t >;

// A destination_t resolved against the raft group, ready for the client factory to send on. Read it
// error-first: !resolved_dest -> the destination could not be resolved (e.g. no known leader); otherwise
// the inner optional is the target -- a peer_id_t, or std::nullopt meaning "broadcast to every peer".
using resolved_dest = result< std::optional< peer_id_t > >;

} // namespace nuraft_mesg

namespace fmt {
template <>
struct formatter< nuraft_mesg::group_id_t > {
    template < typename ParseContext >
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template < typename FormatContext >
    auto format(nuraft_mesg::group_id_t const& n, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", boost::uuids::to_string(n));
    }
};

template <>
struct formatter< nuraft::cmd_result_code > {
    template < typename ParseContext >
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template < typename FormatContext >
    auto format(nuraft::cmd_result_code const& c, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", int32_t(c));
    }
};
} // namespace fmt
