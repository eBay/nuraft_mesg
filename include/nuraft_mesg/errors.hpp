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
#pragma once

// The public error surface for nuraft_mesg. libnuraft's cmd_result_code has ~14 values, but only a few
// are things a caller can act on differently; the rest are either handled internally (retry on
// CONFIG_CHANGING/SERVER_IS_JOINING, idempotent ADD/REM on ALREADY_EXISTS/NOT_FOUND) or collapse to a
// single failure. We surface the result as a portable std::error_condition: the universal cases ride
// std::errc (no nuraft header needed to compare), and the two genuinely raft-domain cases get a tiny
// category. See to_condition() for the full mapping.

#include <string>
#include <system_error>

#include <libnuraft/async.hxx> // nuraft::cmd_result_code

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
    case nuraft::cmd_result_code::TERM_MISMATCH: // a superseded write; from the caller's view, cancelled
        return std::errc::operation_canceled;
    case nuraft::cmd_result_code::TIMEOUT:
        return std::errc::timed_out;
    case nuraft::cmd_result_code::NOT_LEADER:
        return make_error_condition(errc::not_leader);
    default:
        return make_error_condition(errc::failed);
    }
}

} // namespace nuraft_mesg

namespace std {
template <>
struct is_error_condition_enum< nuraft_mesg::errc > : true_type {};
} // namespace std
