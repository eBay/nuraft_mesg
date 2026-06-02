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

// Small coroutine bridges used to turn nuraft_mesg's callback-driven control-plane completions into
// sisl::async::task (exec::task) results, replacing the old std::promise/std::future plumbing. Kept
// nuraft_mesg-local (not in sisl) on purpose -- they are thin adapters over sisl::async::value_awaitable.

#include <atomic>
#include <memory>
#include <utility>

#include <sisl/async/task.hpp>
#include <sisl/async/value_awaitable.hpp>

namespace nuraft_mesg {

// A task that is already resolved with `v`. Used for the synchronous early-out paths (validation
// failures) that previously did `std::promise<>::set_value(...)` then returned the ready future.
template < typename V >
sisl::async::task< V > make_ready(V v) {
    co_return std::move(v);
}

// Bridge a shared value_awaitable to a co_await-able task. The producer (a nuraft / gRPC callback on
// another thread) calls av->complete(value); this task co_awaits it. av is captured BY VALUE so a copy
// lives in the coroutine frame -- value_awaitable is non-movable and the producer holds its address, so
// both sides must keep the same shared object alive (the std::future shared-state pattern).
template < typename V >
sisl::async::task< V > await_value(std::shared_ptr< sisl::async::value_awaitable< V > > av) {
    co_return co_await *av;
}

// A first-wins, single-shot signal shared between two producers: the real event (a config-change /
// leadership nuraft callback) and a deadline timer. Whichever calls signal() first delivers its value to
// the awaiting coroutine; the loser is a no-op. complete() must be called exactly once, so the atomic flag
// guards it. Carries a bool: true = a real config change woke us, false = the deadline timer fired.
struct wakeup_event {
    sisl::async::value_awaitable< bool > _av{};
    std::atomic< bool > _fired{false};

    void signal(bool real) noexcept {
        if (!_fired.exchange(true, std::memory_order_acq_rel)) { _av.complete(real); }
    }
};

} // namespace nuraft_mesg
