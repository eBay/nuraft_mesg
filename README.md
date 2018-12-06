# raft_core_grpc

RaftCoreGRPC is a gRPC service for [raft_core](https://github.corp.ebay.com/nukvengine/raft_core)

## Brief

This project provides a custom RPC implementation for gRPC to raft_core as an alternative
to the internal ASIO implementation.

## Usage

This library only provides the RPC service for raft_core. It provides the following:

* `raft_core::grpc_client : public raft_core::rpc_client`: Initiates RPC calls to remote members.
* `raft_core::grpc_factory : public raft_core::rpc_client_factory`: Client creation factory.
* `raft_core::grpc_server`: RPC handler

You must still provide the following:

* `raft_core::state_machine`: Provides hooks to implement `commit()`, `snapshot()`, `rollback()` etc.
* `raft_core::state_mgr`: RAFT state persistence. Loads/Stores state for the state_machine.
* `raft_core::logger`: Provides logging facility to raft_core for debugging.

A simple echo server can be found in `test_package/example_{client,server}.cpp`

# Conan builds

This project is typically build from a combination of conan.io and CMake.
```
$ pip install -U conan
$ pip remote add conan-sds http://conan-sds.dev.ebayc3.com:9300
$ conan create . dev/$(whoami)
```
