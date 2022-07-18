#!/usr/bin/env python
# -*- coding: utf-8 -*-

from conans import ConanFile, CMake, tools, RunEnvironment
import os


class TestPackageConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    fail_timeout = '30s'
    abort_timeout = '35s'

    requires = (
            "nlohmann_json/3.8.0",
            "jungle_logstore/2022.07.18@sds/master"
        )

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        with tools.environment_append(RunEnvironment(self).vars):
            # TODO: Temporarily restricting tests to run for one build_type only, since running multiple
            # at the same time cause the tests and builds to fail
            if self.settings.build_type == 'Debug':
                self.run("echo $(pwd)")
                bin_path = os.path.join("../../", "run_tests.sh")
                if self.settings.os == "Windows":
                    self.run(bin_path)
                elif self.settings.os == "Macos":
                    self.run("DYLD_LIBRARY_PATH=%s %s" % (os.environ.get('DYLD_LIBRARY_PATH', ''), bin_path))
                else:
                    bin_path = "timeout -k {} {} {}".format(self.abort_timeout, self.fail_timeout, bin_path)
                    self.run("LD_LIBRARY_PATH=%s %s" % (os.environ.get('LD_LIBRARY_PATH', ''), bin_path))
