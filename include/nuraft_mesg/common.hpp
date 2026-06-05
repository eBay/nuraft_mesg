#pragma once

#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <variant>

#include <boost/container/small_vector.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <libnuraft/async.hxx>
#include <sisl/fds/buffer.hpp>
#include <sisl/logging/logging.h>
#include <sisl/async/task.hpp>

SISL_LOGGING_DECL(nuraft_mesg)

#define NURAFTMESG_LOG_MODS nuraft_mesg, grpc_server

namespace sisl {
class GenericClientResponse;
} // namespace sisl

namespace nuraft_mesg {

// The two conditions with no portable std::errc equivalent.
enum class errc {
    not_leader = 1, // this node is not the raft leader for the group; route to / retry on the leader
    failed,         // a raft operation failed for a reason the caller cannot act on differently
};

class error_category : public std::error_category {
public:
    const char* name() const noexcept override { return "nuraft_mesg"; }
    std::string message(int ev) const override {
        switch (static_cast< errc >(ev)) {
        case errc::not_leader:
            return "not the raft leader";
        case errc::failed:
            return "raft operation failed";
        }
        return "unknown nuraft_mesg error";
    }
};

inline const std::error_category& nuraft_mesg_category() noexcept {
    static const error_category cat;
    return cat;
}

inline std::error_condition make_error_condition(errc e) noexcept {
    return {static_cast< int >(e), nuraft_mesg_category()};
}

// Collapse a raw libnuraft result code to the portable condition surface. The internally-handled codes
// (CONFIG_CHANGING/SERVER_IS_JOINING retry, ALREADY_EXISTS/NOT_FOUND idempotency, RESULT_NOT_EXIST_YET
// pending) are resolved before this is ever called; if one slips through it lands in `failed`.
inline std::error_condition to_condition(nuraft::cmd_result_code code) noexcept {
    switch (code) {
    case nuraft::cmd_result_code::OK:
        return {};
    case nuraft::cmd_result_code::BAD_REQUEST:
        return std::errc::invalid_argument;
    case nuraft::cmd_result_code::CANCELLED:
    case nuraft::cmd_result_code::TERM_MISMATCH:
        return std::errc::operation_canceled;
    case nuraft::cmd_result_code::TIMEOUT:
        return std::errc::timed_out;
    case nuraft::cmd_result_code::NOT_LEADER:
        return make_error_condition(errc::not_leader);
    default:
        return make_error_condition(errc::failed);
    }
}

using peer_id_t = boost::uuids::uuid;
using group_id_t = boost::uuids::uuid;
using group_type_t = std::string;
using svr_id_t = int32_t;
using io_blob_list_t = boost::container::small_vector< sisl::io_blob, 4 >;

template < typename T >
using result = std::expected< T, std::error_condition >;
using null_result = result< void >;

// Map a raw libnuraft result code to a null_result: OK -> success, otherwise the collapsed condition.
// The internally-handled codes are resolved before reaching here.
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

namespace std {
template <>
struct is_error_condition_enum< nuraft_mesg::errc > : true_type {};
} // namespace std

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
