#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake, tools

class RaftCoreGRPCConan(ConanFile):
    name = "raft_core_grpc"
    version = "0.3.2"

    license = "Apache 2.0"
    url = "https://github.corp.ebay.com/SDS/raft_core_grpc"
    description = "A gRPC service for raft_core"

    settings = "arch", "os", "compiler", "build_type"
    options = {"shared": ['True', 'False'],
               "fPIC": ['True', 'False'],
               "coverage": ['True', 'False']}
    default_options = 'shared=False', 'fPIC=True', 'coverage=False'

    requires = (("grpc/1.14.1@oss/stable"),
                ("raft_core/2018.08.20@oss/stable"))

    generators = "cmake"
    exports = ["LICENSE.md"]
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*"

    def configure(self):
        if not self.settings.compiler == "gcc":
            del self.options.coverage


    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON'}
        test_target = None

        if self.options.coverage == 'True':
            definitions['CONAN_BUILD_COVERAGE'] = 'ON'
            test_target = 'coverage'

        cmake.configure(defs=definitions)
        cmake.build()
        cmake.test(target=test_target)

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
        if self.options.coverage == 'True':
            self.cpp_info.libs.append('gcov')
