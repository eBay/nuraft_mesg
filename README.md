# nuRAFT-Messaging

[![Conan Build](https://github.com/eBay/nuraft_mesg/actions/workflows/merge_build.yml/badge.svg?branch=dev%2Fv5.x)](https://github.com/eBay/nuraft_mesg/actions/workflows/merge_build.yml)
[![CodeCov](https://codecov.io/gh/eBay/nuraft_mesg/branch/dev%2Fv5.x/graph/badge.svg)](https://codecov.io/gh/eBay/nuraft_mesg)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

> A multi-group service layer for [nuRAFT](https://github.com/eBay/nuraft) — many independent RAFT
> consensus groups multiplexed over a single gRPC server, with a C++20/23 coroutine control- and data-plane.

nuraft_mesg is middleware for nuRAFT: it runs many `nuraft::raft_server` instances behind **one** gRPC
server and a shared, cached client pool. Message routing and a `global` thread pool for the raft
background work are handled by the library, so scaling is bound only by persistence (log stores, state
snapshots) and network — not by per-group threads or sockets.

As of **v5** the control plane and the data-service path are C++20/23 stackless coroutines built on the
[sisl](https://github.com/eBay/sisl) `async` substrate and [NVIDIA stdexec](https://github.com/NVIDIA/stdexec)
(P2300); the public API is uniformly `lower_snake_case` and every fallible call returns a
`std::error_condition`. The old `std::future` / Folly surface is gone — see the [Changelog](CHANGELOG.md).

## 🚀 Features

- **Multi-group multiplexing** — N independent RAFT groups share one gRPC server and one cached
  client-connection pool; a join request for an unknown group instantiates that group on demand.
- **Coroutine control plane** — `create_group` / `add_member` / `rem_member` / `become_leader` /
  `append_entries` are awaitable (`null_async_task`); transient raft states (`CONFIG_CHANGING`,
  `SERVER_IS_JOINING`) are retried internally so consumers don't hand-roll retry loops.
- **Coroutine data service** — an optional application-level request/response channel between replicas
  (`repl_service_ctx::data_service_request_*`), uni- and bidirectional, addressed by peer / leader / all.
- **One error type** — `result<T> = std::expected<T, std::error_condition>`; a tiny `errc` domain carries
  the only consumer-actionable cases (`not_leader`, `failed`), the rest map onto `std::errc`.
- **Clean extension surface** — implement `messaging_application` and subclass `mesg_state_mgr`; the
  gRPC server, client factories and the `nuraft::raft_server` wiring stay internal.
- **stdexec shipped transitively** — provided through the sisl package; consumers don't FetchContent or
  depend on stdexec directly.
- Validated under **ASan / TSan**.

## 📋 Table of Contents

- [Quick Start](#-quick-start)
- [Architecture](#-architecture)
- [Coroutine API](#-coroutine-api)
- [Usage](#-usage)
- [Development](#-development)
- [Testing](#-testing)
- [Dependencies](#-dependencies)
- [Documentation](#-documentation)
- [License](#-license)

## 🏃 Quick Start

### Prerequisites

- Linux
- Conan 2.x · CMake 3.22+ · C++23 compiler (**GCC 13+** or **Clang 17+**)
- The [sisl](https://github.com/eBay/sisl) recipe (`sisl/<ver>@oss/dev`) in your Conan cache or a remote

### Build & Test

```bash
git clone https://github.com/eBay/nuraft_mesg
cd nuraft_mesg
./prepare_v2.sh                                   # export vendored recipes: forestdb, jungle, nuraft
conan create -s:h build_type=Debug --build missing .   # builds + runs the ctest suite + test_package
```

### Build Options

```bash
# Release
conan create -s:h build_type=Release --build missing .

# Address / thread sanitizer
conan create -s:h build_type=Debug -o nuraft_mesg/*:sanitize=address --build missing .
conan create -s:h build_type=Debug -o nuraft_mesg/*:sanitize=thread  --build missing .

# Coverage
conan create -s:h build_type=Debug -o nuraft_mesg/*:coverage=True --build missing .
```

## 🏗️ Architecture

```
nuraft_mesg/
├── include/nuraft_mesg/        # Public headers (installed)
│   ├── nuraft_mesg.hpp           # manager + messaging_application + init_messaging (entry point)
│   ├── mesg_state_mgr.hpp        # mesg_state_mgr (extension base) + repl_service_ctx (per-group session)
│   ├── mesg_factory.hpp          # client-side group_factory / mesg_factory (talk to a group directly)
│   ├── common.hpp                # vocabulary: ids, result<T> / async_task<T>, destination_t
│   └── errors.hpp                # errc domain + cmd_result_code → std::error_condition mapping
├── src/
│   ├── lib/                    # Internal: ManagerImpl, msg_service, grpc_server, repl_service_ctx_grpc
│   ├── proto/                  # gRPC service definition + messaging client
│   └── tests/                  # GoogleTest suites
└── test_package/              # consumer smoke test: example_{server,client}.cpp
```

### Core Abstractions

| Type / entry point | Role |
|---|---|
| `init_messaging(params, app, with_data_svc)` | Bring up the `manager` — one gRPC server for every group |
| `manager` | Multi-group lifecycle: `create_group` / `add_member` / `rem_member` / `become_leader` / `append_entries` (all `null_async_task`) |
| `messaging_application` | Consumer node hooks: `lookup_peer(id)` → endpoint, `create_state_mgr(srv_id, group)` → a `mesg_state_mgr` |
| `mesg_state_mgr` | Consumer's per-group state manager (extends `nuraft::state_mgr`): `get_state_machine`, `raft_event`, lifecycle; `repl_ctx()` for the session |
| `repl_service_ctx` | Per-group session provided by the library: data-service requests + raft status / config / leader |
| `mesg_factory` | Client-side factory for sending to a group without joining it |
| `result<T>` / `null_result` | `std::expected<T, std::error_condition>` — the unified error surface |

## 🧬 Coroutine API

Control-plane and data-service calls are awaitable; `co_await` yields a `result` (`std::expected`). Note
that lazy coroutines must take their parameters **by value** (a `const&` would dangle once the task is
co_awaited later):

```cpp
#include <nuraft_mesg/nuraft_mesg.hpp>
using namespace nuraft_mesg;

// Grow a group and await the outcome — linear control flow, no futures or callbacks.
null_async_task grow(std::shared_ptr< manager > mgr, group_id_t group, peer_id_t new_peer) {
    if (auto r = co_await mgr->add_member(group, new_peer); !r) {
        co_return std::unexpected(r.error());          // r.error() is a std::error_condition
    }
    co_return null_result{};
}
```

Data-service requests go through the per-group `repl_service_ctx` and are addressed by a `destination_t`
(a specific `peer_id_t`, `role_regex::LEADER`, or `role_regex::ALL`):

```cpp
// Inside a mesg_state_mgr, send an application payload to the current leader.
auto r = co_await repl_ctx()->data_service_request_unidirectional(role_regex::LEADER, "my_request", bufs);
```

The coroutine / stdexec machinery (`sisl::async::task`, senders) sits behind the `async_task` aliases;
consumers compose with `co_await` and never need to depend on stdexec directly.

## 🖥️ Usage

To bring a node up you implement two interfaces and register them with `init_messaging`:

- **`messaging_application`** (one per node) — `lookup_peer(peer_id)` resolves a peer to a gRPC
  endpoint, and `create_state_mgr(srv_id, group_id)` instantiates your per-group state manager when a
  group is created locally or a join arrives for one this node doesn't have yet.
- **`mesg_state_mgr`** (one per group, subclassed) — RAFT state persistence (extends
  `nuraft::state_mgr`) plus `get_state_machine()` and the group lifecycle hooks.

You also still provide the two nuRAFT pieces:

- **`nuraft::state_machine`** — your `commit()` / `snapshot()` / `rollback()` logic.
- **`nuraft::log_store`** — e.g. [Jungle](https://github.com/eBay/Jungle) or
  [HomeStore](https://github.com/eBay/HomeStore).

A runnable echo server + client live in `test_package/example_{server,client}.cpp`.

## 🛠️ Development

### Code Style

- **Indentation:** 4 spaces · **Line length:** 120 · **Pointers:** left-aligned (`Type* p`)
- **Standard:** C++23 · **Headers:** `#pragma once`
- Run `./apply-clang-format.sh` (if present) before submitting.

### Naming Conventions

| Element | Convention | Example |
|---|---|---|
| **Public API** (`include/nuraft_mesg/`) | `lower_snake_case` | `manager`, `messaging_application`, `mesg_state_mgr`, `repl_service_ctx`, `init_messaging` |
| **Internal classes** (`src/lib/`, not installed) | existing names | `ManagerImpl`, `msg_service`, `grpc_server`, `repl_service_ctx_grpc` |
| Functions / methods | `snake_case` | `create_group`, `add_member`, `data_service_request_unidirectional` |
| Members | `m_snake_case` / `_snake_case` | `m_repl_svc_ctx`, `_manager` |

The v5 public surface is uniformly `lower_snake_case`; internal implementation classes keep their
existing names since they never reach a consumer.

### Error Handling

One error type across the public surface — a value on success, a `std::error_condition` on failure:

```cpp
using result = std::expected< T, std::error_condition >;   // null_result == result< void >

if (auto r = co_await mgr->add_member(group, peer); !r) {
    LOGERROR("add_member failed: {}", r.error().message());
    if (r.error() == errc::not_leader) { /* re-issue on the leader */ }
}
```

Transient raft codes (`CONFIG_CHANGING`, `SERVER_IS_JOINING`) are retried inside the library;
`ALREADY_EXISTS` (add) and `SERVER_NOT_FOUND` (remove) are treated as idempotent success.

## 🧪 Testing

GoogleTest suites under `src/tests/` plus the `test_package` consumer build run automatically as part of
`conan create`. The suites spin real multi-node raft clusters over loopback and exercise both the
control plane (`raft_service_test`) and the data service (`data_service_test`).

```bash
# Build + run the suite
conan create -s:h build_type=Debug --build missing .

# Under sanitizers
conan create -s:h build_type=Debug -o nuraft_mesg/*:sanitize=address --build missing .
conan create -s:h build_type=Debug -o nuraft_mesg/*:sanitize=thread  --build missing .
```

## 📦 Dependencies

### Core

- **[sisl](https://github.com/eBay/sisl)** (v14+) — logging, options, metrics, the gRPC wrappers, and the
  `async` coroutine substrate; it also ships **[NVIDIA stdexec](https://github.com/NVIDIA/stdexec)**
  (P2300) transitively, so consumers get it without their own FetchContent.
- **[nuRAFT](https://github.com/eBay/nuraft)** (2.4+) — the RAFT engine (exported by `prepare_v2.sh`).
- **gRPC** — transport for the raft and data-service channels.

### Test / Tooling

- **[Jungle](https://github.com/eBay/Jungle)** / **forestdb** — log store used by the tests (exported by `prepare_v2.sh`).
- **gtest / gmock** — test framework.
- **Conan** 2.x · **CMake** 3.22+ · **GCC 13+ / Clang 17+** · **clang-format**.

## 📚 Documentation

- **[CHANGELOG.md](CHANGELOG.md)** — version history (the v4 → v5 coroutine/API changes).
- **[CLAUDE.md](CLAUDE.md)** — build commands and repository guidance.

## 📄 License

Copyright 2021 eBay Inc.

Primary Author: Brian Szmyd

Primary Developers:
 [Brian Szmyd](https://github.com/szmyd),
 [Ravi Nagarjun Akella](https://github.com/raakella1),
 [Harihara Kadayam](https://github.com/hkadayam)

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the
License. You may obtain a copy of the License at https://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
