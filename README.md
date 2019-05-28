# nupillar_grpc

RaftCoreGRPC is a gRPC service for [nupillar](https://github.corp.ebay.com/SDS/nupillar)

## Brief

This project provides a custom RPC implementation for gRPC to nupillar as an alternative
to the internal ASIO implementation.

## Changes

See the [Changelog](CHANGELOG.md) for release information.

## Usage

This library only provides the RPC service for nupillar. It provides the following:

* `sds::grpc_client : public nupillar::rpc_client`: Initiates RPC calls to remote members.
* `sds::grpc_factory : public nupillar::rpc_client_factory`: Client creation factory.
* `sds::grpc_server`: RPC handler

You must still provide the following:

* `nupillar::state_machine`: Provides hooks to implement `commit()`, `snapshot()`, `rollback()` etc.
* `nupillar::state_mgr`: RAFT state persistence. Loads/Stores state for the state_machine.
* `nupillar::logger`: Provides logging facility to nupillar for debugging.

A simple echo server can be found in `test_package/example_{client,server}.cpp`

# Conan builds

This project is typically build from a combination of conan.io and CMake.
```
$ pip install -U conan
$ pip remote add conan-sds http://conan-sds.dev.ebayc3.com:9300
$ conan create . dev/$(whoami)
```
