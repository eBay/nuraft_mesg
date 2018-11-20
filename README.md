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

A simple echo server can be found in `test_package/example_{client,server}.cpp`

# Conan builds

This project is typically build from a combination of conan.io and CMake.
The current existing platform/builds that exist:
```
$ pip install -U conan
$ pip remote add conan-sds http://conan-sds.dev.ebayc3.com:9300
$ conan search -r conan-sds 'raft_core_grpc/0.0.1@sds/testing'
Existing packages for recipe raft_core_grpc/0.0.1@sds/testing:

    Package_ID: 75e9c8f90412311bce7bc64007e740d05e2d7b46
        [options]
            fPIC: True
            shared: False
        [settings]
            arch: x86_64
            build_type: RelWithDebInfo
            compiler: gcc
            compiler.libcxx: libstdc++11
            compiler.version: 7
            os: Linux
        [requires]
            boost/1.67.0@oss/stable:aa10c32850b15ef5456122906d38ebd7c80ab68c
            c-ares/1.14.0@oss/stable:af7901d8bdfde621d086181aa1c495c25a17b137
            grpc/1.12.1@oss/stable:85787976f60a36dfd78adbf211e5d776909f22cc
            protobuf/3.5.2@oss/stable:aa10c32850b15ef5456122906d38ebd7c80ab68c
            raft_core/0.1.0@sds/stable:4648651fbdcf52b9ce2af636280053c2d1782ddf
        Outdated from recipe: False

    Package_ID: 7df76e356a5abe3834cf63a61aeda79e560bbefb
        [options]
            fPIC: True
            shared: False
        [settings]
            arch: x86_64
            build_type: Debug
            compiler: clang
            compiler.libcxx: libstdc++11
            compiler.version: 6.0
            os: Linux
    ...
```
