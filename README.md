# nuRAFT-Messaging
![merge_conan_build](https://github.com/ebay/nuraft_mesg/actions/workflows/merge_conan_build.yml/badge.svg)

## Brief

A multi-group service layer for [nuraft](https://github.com/eBay/nuraft)

Provides a middleware to nuRaft which manages multple NuRaft servers all multiplex'd through a single server instance
and cache'd client pairs.

## Changes

See the [Changelog](CHANGELOG.md) for release information.

## Usage

The `nuraft::service` provides a means to register callbacks which instantiate `nuraft::state_manager`s. This callback
is used whenever a group is directly created (`service::create_group(...)` ) or when the `grpc::Service` receives a
`RaftMessage` indicating a request to join a group which one does not already belong.

Routing of messages is handled by this library and uses a `global` thread pool for `nuraft::raft_server` bg threads.
Therefore scaling is only bound by available persistence (for logstores and state snapshots), and network resources.

You must still provide the following:

* `nuraft::state_machine`: Provides hooks to implement `commit()`, `snapshot()`, `rollback()` etc.
* `nuraft::log_store`: Logstore (e.g. [Jungle](https://github.com/eBay/Jungle) or [Homestore](https://github.com/ebay/Homestore)).
* `nuraft_mesg::mesg_state_mgr`: RAFT state persistence. Loads/Stores state for the state_machine, snapshots etc.

A simple echo server and client can be found in `test_package/example_{client,server}.cpp`

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

## License Information
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
