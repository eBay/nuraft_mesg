#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class NuRaftGRPCConan(ConanFile):
    name = "nuraft_grpc"
    version = "0.11.10"

    license = "Apache 2.0"
    url = "https://github.corp.ebay.com/SDS/nuraft_grpc"
    description = "A gRPC service for nuraft"

    settings = "arch", "os", "compiler", "build_type"
    options = {
        "shared": ['True', 'False'],
        "fPIC": ['True', 'False'],
        }
    default_options = (
        'shared=False',
        'fPIC=True',
        )

    requires = (
            "lzma/5.2.4@bincrafters/stable",
            "nuraft/2019.08.23@oss/testing",
            "sds_grpc/1.1.8@sds/testing",
            "sds_logging/5.3.2@sds/testing",
        )

    generators = "cmake"
    exports = ["LICENSE.md"]
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*"

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON'}
        test_target = None

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
        self.copy("*.h", dst="include/nuraft_grpc", keep_path=False)
        self.copy("*.hpp", dst="include/nuraft_grpc", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
