# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Project Is

**nuraft_mesg** is a multi-group gRPC service layer that wraps [nuRAFT](https://github.com/eBay/nuraft) (eBay's C++ RAFT implementation). It multiplexes multiple independent RAFT consensus groups over a single gRPC server instance, sharing a thread pool and connection cache across all groups. It also provides an optional data service channel for application-level streaming between replicas.

## Build System

This project uses **Conan 2 + CMake** (C++23). There is no standalone Makefile.

### One-time setup
```sh
./prepare_v2.sh   # exports vendored recipes: forestdb, jungle, nuraft
```

### Build + test (Debug with libc)
```sh
conan create -o sisl/*:malloc_impl=libc -s:h build_type=Debug --build missing .
```

### Build only (skip tests)
```sh
conan install -o sisl/*:malloc_impl=libc -s:h build_type=Debug -c tools.build:skip_test=True --build missing .
```

### Sanitizers (ASan + UBSan)
```sh
conan create -o sisl/*:malloc_impl=libc -o nuraft_mesg/*:sanitize=address -s:h build_type=Debug --build missing .
```

### Coverage
```sh
conan build -o sisl/*:malloc_impl=libc -o nuraft_mesg/*:coverage=True -s:h build_type=Debug --build missing .
gcovr --cobertura ./coverage.xml
```

### Formatting
```sh
./apply-clang-format.sh        # apply in-place
./apply-clang-format.sh -v     # validate (exits 1 on violations)
```

## Running Tests

Build output is in `build/Debug/` (or `build/Release/`, etc.).

```sh
# Run all tests via CTest
ctest --test-dir build/Debug -V

# Run a single named test suite
ctest --test-dir build/Debug -R RaftServiceTest -V
ctest --test-dir build/Debug -R DataServiceTest -V

# Run a specific GTest case within a binary
./build/Debug/src/tests/raft_service_test --gtest_filter="MessagingFixture.BasicTests"
./build/Debug/src/tests/data_service_test --gtest_filter="DataServiceFixture.BasicTest1"
```

Both test binaries accept `-cv <level>` for console logging verbosity and standard `--gtest_filter` for filtering.

## Architecture

### Core Abstractions

**`manager`** (`include/nuraft_mesg/nuraft_mesg.hpp`) — the public facade. Obtained via:
```cpp
std::shared_ptr<manager> init_messaging(manager::params const&, std::weak_ptr<messaging_application>, bool with_data_svc = false);
```
Control operations (`create_group`, `add_member`, `rem_member`, `become_leader`, `append_entries`) return `null_async_task` and are `co_await`-ed; plus `bind_data_service_request`, `leave_group`.

**`messaging_application`** (user implements) — Strategy interface:
- `lookup_peer(peer_id_t)` → endpoint string
- `create_state_mgr(srv_id, group_id)` → `std::shared_ptr<mesg_state_mgr>`

**`mesg_state_mgr`** (user extends, `include/nuraft_mesg/mesg_state_mgr.hpp`) — extends `nuraft::state_mgr` with RAFT lifecycle callbacks (`raft_event()`), `get_state_machine()`, and the persistence hooks (`load_config`, `save_config`, `load_log_store`, etc.). The per-group session is reached via `repl_ctx()`; the internal wiring (`make_repl_ctx`, `set_manager_impl`, `internal_raft_event_handler`) is private.

**`repl_service_ctx`** — the per-group session the library provides; `is_raft_leader()`, `data_service_request_unidirectional/bidirectional()`, `send_data_service_response()`, `get_cluster_config()`, `get_raft_status()`, and `raft_server()` for direct nuraft access.

### Key Types (`include/nuraft_mesg/common.hpp`)
```cpp
using peer_id_t   = boost::uuids::uuid;
using group_id_t  = boost::uuids::uuid;
using group_type_t = std::string;

template<typename T> using result     = std::expected<T, std::error_condition>;  // errors.hpp: errc domain
template<typename T> using async_task  = sisl::async::task<result<T>>;            // exec::task coroutine

using null_result     = result<void>;
using null_async_task = async_task<void>;

using destination_t = std::variant<peer_id_t, role_regex, svr_id_t>;
ENUM(role_regex, uint8_t, LEADER, FOLLOWER, ALL, ANY);
```
All async paths are coroutines (`sisl::async::task` over stdexec); failures surface as `std::error_condition`, not exceptions or `std::future`.

### Internal Structure

```
src/lib/          # Core implementation
  manager_impl    # ManagerImpl: owns the gRPC server, msg_service, group state map
  service         # msg_service: routes inbound gRPC to the correct nuraft::raft_server
  grpc_server     # Thin wrapper around nuraft::raft_server
  factory         # grpc_factory: caches gRPC clients, shared across groups
  data_service_grpc  # Server-side dispatch for named data service handlers
  repl_service_ctx   # Client-side data service context injected into state managers

src/proto/        # Protobuf serialization backend (default)
src/flatb/        # FlatBuffers alternative serialization backend

src/tests/        # GTest tests; shared fixture in test_fixture.ipp (3-node cluster)
  jungle_logstore/ # Test-only Jungle-backed log store
```

### Factory Hierarchy
```
nuraft::rpc_client_factory
  └── grpc_factory          (client cache, worker threads)
        └── group_factory   (SSL, auth, endpoint lookup via messaging_application)
              └── mesg_factory (per-group)
```

### Internal Routing
`msg_service` holds a `std::unordered_map<group_id_t, grpc_server_wrapper>` (guarded by `std::shared_mutex`) to route inbound RAFT RPCs to the correct group's server. Data service handler dispatch keys on `"request_name|group_id"` strings.

## CI Build Matrix

Three effective build combinations run in CI:
1. Debug + libc + AddressSanitizer/UBSan
2. Debug + libc + Coverage (gcovr → Codecov)
3. Release + tcmalloc (no extra tooling)

## Key Dependencies

| Dependency | Role |
|---|---|
| `sisl` | eBay utility lib: gRPC server/client, logging, metrics, buffer types (`sisl::io_blob`) |
| `nuraft` | RAFT consensus engine (vendored at `2.4.9`) |
| `jungle` | Log store used in tests (vendored) |
