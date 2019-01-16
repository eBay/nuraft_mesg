# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### None

## [0.8.2] - 2019.01.15
### Changed
- FIX: Return no address if srv_id is not found in raft_server config.

## [0.8.1] - 2019.01.15
### Added
- New `raft_server()` call on _grpc_server_ to get underlying server.

## [0.8.0] - 2019.01.12
### Added
- Factory requests now require a `cornerstone::srv_config` for the intial client creation.
- New `dest_address` field in response message to use for trying a new leader.

### Changed
- Use the `dest_address` returned from a non-leader as the srv address for the new connection.

### Removed
- Removed the lookup_address virtual function in preference of the new proto field.

## [0.7.5] - 2019.01.11
### Changed
- Fix, use lookup_address on current_leader when creating clients from factory funcs.

## [0.7.4] - 2019.01.11
### Added
- This CHANGELOG.md to adhere to changelog standards.
- `lookup_address()` call to `class grpc_factory` to convert between srv_id and srv_addr

### Changed
- Upgraded [sds_logging](https://github.corp.ebay.com/SDS/sds_logging) to 3.6.0

[Unreleased]: https://github.corp.ebay.com/SDS/raft_core_grpc/compare/testing/v0.x...develop
[0.8.2]: https://github.corp.ebay.com/SDS/raft_core_grpc/compare/bf53a8f...testing/v0.x
[0.8.1]: https://github.corp.ebay.com/SDS/raft_core_grpc/compare/d12747d...bf53a8f
[0.8.0]: https://github.corp.ebay.com/SDS/raft_core_grpc/compare/5e8915d...d12747d
[0.7.5]: https://github.corp.ebay.com/SDS/raft_core_grpc/compare/ebcee31...5e8915d
[0.7.4]: https://github.corp.ebay.com/SDS/raft_core_grpc/compare/8a5a11a...ebcee31
