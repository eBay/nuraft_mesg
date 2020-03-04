#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class NuRaftGRPCConan(ConanFile):
    name = "nuraft_grpc"
    version = "0.14.1"

    license = "Apache 2.0"
    url = "https://github.corp.ebay.com/SDS/nuraft_grpc"
    description = "A gRPC service for nuraft"
    revision_mode = "scm"

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
            "nuraft/1.4.6@sds/develop",
            "sds_grpc/1.1.11@sds/master",
            "sds_logging/6.1.2@sds/develop"
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
