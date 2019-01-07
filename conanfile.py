#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class RaftCoreGRPCConan(ConanFile):
    name = "raft_core_grpc"
    version = "0.7.3"

    license = "Apache 2.0"
    url = "https://github.corp.ebay.com/SDS/raft_core_grpc"
    description = "A gRPC service for raft_core"

    settings = "arch", "os", "compiler", "build_type", "sanitize"
    options = {
        "shared": ['True', 'False'],
        "fPIC": ['True', 'False'],
        "coverage": ['True', 'False'],
        }
    default_options = (
        'shared=False',
        'fPIC=True',
        'coverage=False',
        )

    requires = (
            "boost_container/1.67.0@bincrafters/stable",
            "gtest/1.8.1@bincrafters/stable",
            "lzma/5.2.4@bincrafters/stable",
            "OpenSSL/1.0.2q@conan/stable",
            "raft_core/2018.12.21@oss/testing",
            "sds_grpc/1.0.3@sds/testing",
            "sds_logging/3.5.2@sds/testing"
        )

    generators = "cmake"
    exports = ["LICENSE.md"]
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*"

    def configure(self):
        if not self.settings.compiler == "gcc":
            del self.options.coverage
        elif self.settings.sanitize != None:
            self.options.coverage = 'False'

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        if self.settings.sanitize != None:
            definitions['MEMORY_SANITIZER_ON'] = 'ON'
        elif self.options.coverage == 'True':
            definitions['CONAN_BUILD_COVERAGE'] = 'ON'
            test_target = 'coverage'

        cmake.configure(defs=definitions)
        cmake.build()
        cmake.test(target=test_target)

    def package(self):
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.dylib*", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.proto", dst="proto/", keep_path=False)
        self.copy("*.h", dst="include/raft_core_grpc", keep_path=False)
        self.copy("*.hpp", dst="include/raft_core_grpc", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        if self.settings.sanitize != None:
            self.cpp_info.sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.exelinkflags.append("-fsanitize=address")
            self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.exelinkflags.append("-fsanitize=undefined")
        elif self.options.coverage == 'True':
            self.cpp_info.libs.append('gcov')
