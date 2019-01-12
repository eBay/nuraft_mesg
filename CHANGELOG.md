# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Changed
- Fix, use lookup_address on current_leader when creating clients from factory funcs.

## [0.7.4] - 2019-01-11
### Added
- This CHANGELOG.md to adhere to changelog standards.
- `lookup_address()` call to `class grpc_factory` to convert between srv_id and srv_addr

### Changed
- Upgraded [sds_logging](https://github.corp.ebay.com/SDS/sds_logging) to 3.6.0

[Unreleased]: https://github.corp.ebay.com/SDS/access-mgr/compare/testing/v0.x...develop
[0.7.4]: https://github.corp.ebay.com/SDS/raft_core_grpc/compare/8a5a11a...testing/v0.x
