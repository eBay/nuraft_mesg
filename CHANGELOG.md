# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Coroutine control- and data-plane: `async_task<T>` / `null_async_task` (`sisl::async::task` over
  NVIDIA stdexec); `manager` control methods and `repl_service_ctx` data-service requests are awaitable.
- `errors.hpp`: an `errc` domain (`not_leader`, `failed`) plus a `cmd_result_code` →
  `std::error_condition` mapping; the universal cases ride `std::errc`.
- `repl_service_ctx::repl_ctx()` accessor for the per-group session.
- stdexec is now consumed as a conan dependency (transitively via sisl) instead of CMake FetchContent.

### Changed
- **Breaking:** the public API is now uniformly `lower_snake_case` — `Manager` → `manager`,
  `MessagingApplication` → `messaging_application`, `Manager::Params` → `manager::params`,
  `Result`/`NullResult` → `result`/`null_result`, `lookupEndpoint` → `lookup_endpoint`.
- **Breaking:** the error surface is `result<T> = std::expected<T, std::error_condition>`; the
  `std::future` / Folly-based public API has been removed.
- Transient raft states (`CONFIG_CHANGING`, `SERVER_IS_JOINING`) are retried inside the library, and
  `ALREADY_EXISTS` (add) / `SERVER_NOT_FOUND` (remove) are treated as idempotent success — consumers no
  longer wrap these calls in retry loops.
- `mesg_state_mgr`'s internal wiring (`make_repl_ctx`, `set_manager_impl`, `internal_raft_event_handler`)
  is hidden from the consumer-facing interface; the factory hierarchy stays internal.
- Build now requires C++23 (GCC 13+ / Clang 17+).

### Removed
- The deprecated `mesg_state_mgr::handle_raft_event` and the `std::future`-based `AsyncResult` /
  `NullAsyncResult` aliases.

[Unreleased]: https://github.com/eBay/nuraft_mesg/compare/stable/v5.x...HEAD
