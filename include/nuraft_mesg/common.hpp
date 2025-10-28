#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <folly/Expected.h>
#include <folly/small_vector.h>
#include <folly/Unit.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <folly/futures/Future.h>
#pragma GCC diagnostic pop

#include <libnuraft/async.hxx>
#include <sisl/fds/buffer.hpp>
#include <sisl/logging/logging.h>

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
using io_blob_list_t = folly::small_vector< sisl::io_blob, 4 >;

template < typename T >
using Result = folly::Expected< T, nuraft::cmd_result_code >;
template < typename T >
using AsyncResult = folly::SemiFuture< Result< T > >;

using NullResult = Result< folly::Unit >;
using NullAsyncResult = AsyncResult< folly::Unit >;

ENUM(role_regex, uint8_t, LEADER, FOLLOWER, ALL, ANY);
using destination_t = std::variant< peer_id_t, role_regex, svr_id_t >;

} // namespace nuraft_mesg

namespace fmt {
template <>
struct formatter< nuraft_mesg::group_id_t > {
    template < typename ParseContext >
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template < typename FormatContext >
    auto format(nuraft_mesg::group_id_t const& n, FormatContext& ctx) {
        return fmt::v10::format_to(ctx.out(), "{}", boost::uuids::to_string(n));
    }
};

template <>
struct formatter< nuraft::cmd_result_code > {
    template < typename ParseContext >
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template < typename FormatContext >
    auto format(nuraft::cmd_result_code const& c, FormatContext& ctx) {
        return fmt::v10::format_to(ctx.out(), "{}", int32_t(c));
    }
};
} // namespace fmt
