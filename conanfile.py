#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools
import os

class NuRaftGRPCConan(ConanFile):
    name = "nuraft_grpc"
    version = "5.3.0"

    license = "Apache 2.0"
    url = "https://github.corp.ebay.com/SDS/nuraft_grpc"
    description = "A gRPC service for nuraft"

    settings = "arch", "os", "compiler", "build_type"

    generators = "cmake"
    requires = (
                "nuraft/[~=1.8, include_prerelease=True]@nudata/master",
                "grpc_helper/[~=2, include_prerelease=True]@sisl/master",
                "sisl/[~=7, include_prerelease=True]@sisl/master",
                ("fmt/8.0.1",       "override"),
                )
    options = {
                "shared": ['True', 'False'],
                "fPIC": ['True', 'False'],
                "sanitize": ['True', 'False'],
                "prerelease": ['True', 'False'],
                }
    default_options = (
                        'shared=False',
                        'fPIC=True',
                        'sanitize=False',
                        'prerelease=True',
                        )

    exports = ["LICENSE.md"]
    exports_sources = (
                        "CMakeLists.txt",
                        "cmake/*",
                        "src/*",
                        )

    def config_options(self):
        if self.settings.build_type != "Debug":
            del self.options.sanitize

    def configure(self):
        self.options['grpc_helper'].prerelease = self.options.prerelease

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        run_tests = True
        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                definitions['MEMORY_SANITIZER_ON'] = 'ON'
            else:
                if (None == os.getenv("RUN_TESTS")):
                    run_tests = False

        cmake.configure(defs=definitions)
        cmake.build()
        if run_tests:
            cmake.test(target=test_target)

    def package(self):
        self.copy("*.h", dst="include/nuraft_grpc", keep_path=False)
        self.copy("*.hpp", dst="include/nuraft_grpc", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.dylib*", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.proto", dst="proto/", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                self.cpp_info.sharedlinkflags.append("-fsanitize=address")
                self.cpp_info.exelinkflags.append("-fsanitize=address")
                self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
                self.cpp_info.exelinkflags.append("-fsanitize=undefined")
