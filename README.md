# raft_core_grpc

RaftCoreGRPC is a gRPC service for [raft_core](https://github.corp.ebay.com/nukvengine/raft_core)

## Brief

This project provides a custom RPC implementation for gRPC to raft_core as an alternative
to the internal ASIO implementation.

## Usage

This library only provides the RPC service for raft_core. It provides the following:

* `raft_core::grpc_client : public raft_core::rpc_client`: Initiates RPC calls to remote members.
* `raft_core::grpc_service : public raft_core::rpc_client_factory`: Member listening service.

You must still provide the following:

* `raft_core::state_machine`: Provides hooks to implement `commit()`, `snapshot()`, `rollback()` etc.
* `raft_core::state_mgr`: RAFT state persistence. Loads/Stores state for the state_machine.

A simple echo server can be found in `test_package/example_{client,server}.cpp`
