#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class RaftCoreGRPCConan(ConanFile):
    name = "raft_core_grpc"
    version = "0.5.0"

    license = "Apache 2.0"
    url = "https://github.corp.ebay.com/SDS/raft_core_grpc"
    description = "A gRPC service for raft_core"

    settings = "arch", "os", "compiler", "build_type"
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
            "grpc/1.16.0@oss/stable",
            "raft_core/0.3.0@oss/stable",
            "sds_logging/3.3.0@sds/stable"
        )

    generators = "cmake"
    exports = ["LICENSE.md"]
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*"
    sanitize = False

    def configure(self):
        if self.settings.build_type == "Debug":
            self.sanitize = True
        if not self.settings.compiler == "gcc":
            del self.options.coverage

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        if self.settings.compiler == "gcc":
            if self.options.coverage == 'True':
                definitions['CONAN_BUILD_COVERAGE'] = 'ON'
                test_target = 'coverage'

        if self.sanitize == 'True':
            definitions['MEMORY_SANITIZER_ON'] = 'ON'

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
        self.copy("*.h", dst="include/cornerstone", keep_path=False)
        self.copy("*.hpp", dst="include/cornerstone", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        if (self.sanitize) :
            self.cpp_info.sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.exelinkflags.append("-fsanitize=address")
            self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.exelinkflags.append("-fsanitize=undefined")
        if self.settings.compiler == "gcc":
            if self.options.coverage == 'True':
                self.cpp_info.libs.append('gcov')
