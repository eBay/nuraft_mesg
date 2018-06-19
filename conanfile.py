#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class RaftCoreGRPCConan(ConanFile):
    name = "raft_core_grpc"
    version = "0.0.1"
    license = "Apache 2.0"
    url = "https://github.corp.ebay.com/SDS/raft_core_grpc"
    description = "A gRPC service for raft_core"

    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = "shared=False", "fPIC=True"

    requires = (("grpc/1.12.1@oss/stable"),
                ("raft_core/0.1.0@sds/stable"))

    generators = "cmake"

    exports = ["LICENSE.md"]
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.test()

    def package(self):
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.dylib*", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)
        self.copy("*.proto", dst="proto/", keep_path=False)
        self.copy("*.h", dst="include/cornerstone", keep_path=False)
        self.copy("*.hpp", dst="include/cornerstone", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        self.cpp_info.libs.append('cornerstone')
