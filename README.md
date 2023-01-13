# nuraft_grpc

## Brief

NuRAFT-gRPC is a Protobuf translation layer for [nuraft](https://github.com/eBay/nuraft)

This project provides a the wiring to use Protobuf for marshalling NuRaft calls between services as an alternative
to the native Boost::ASIO RPC service.

## Changes

See the [Changelog](CHANGELOG.md) for release information.

## Usage

This library only provides the nuraft message marshalling. In essense, it translates NuRaft types into comparable
Protobuf objects which can be used when defining a gRPC service. A Protobuf schema is included for use
with embedding these message types into downstream .proto service files.

You must still provide the following:

* `nuraft::state_machine`: Provides hooks to implement `commit()`, `snapshot()`, `rollback()` etc.
* `nuraft::state_mgr`: RAFT state persistence. Loads/Stores state for the state_machine.
* `nuraft::logger`: Provides logging facility to nuraft for debugging.
* `nuraft_grpc::grpc_client`: Calls `step` on associated gRPC client.
* `nuraft_grpc::grpc_server`: Associates/Binds `step` gRPC call to marshalling calls.

A simple echo server can be found in `test_package/example_{client,server}.cpp`

# Conan builds

## Notes

Currently this project can only be built on Linux using the GCC compiler toolchain. Work is on-going to support other
platforms (e.g. MacOS) using the Clang and VSCode toolchains. Please feel free to contribute changes that support this
endeavour.

## Dependencies

This project depends on the [Symbiosis Library](https://github.com/eBay/sisl) which is currently not available
in conan-center. If using conan-center one must first export this recipe to their local conan cache, example:
```
$ git clone https://github.com/eBay/sisl sisl
$ conan export sisl/ oss/master
```

## Building the Package

This project is typically built from a combination of conan.io and CMake (which must be installed on the host).
```
$ pip install -U conan
$ conan create --build missing . <user>/<channel>
```
