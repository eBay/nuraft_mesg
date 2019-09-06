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
            "jsonformoderncpp/3.6.1@vthiery/stable",
            "jungle_logstore/2019.09.05@sds/testing",
        )

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        with tools.environment_append(RunEnvironment(self).vars):
            self.run("echo $(pwd)")
            bin_path = os.path.join("../../", "run_tests.sh")
            if self.settings.os == "Windows":
                self.run(bin_path)
            elif self.settings.os == "Macos":
                self.run("DYLD_LIBRARY_PATH=%s %s" % (os.environ.get('DYLD_LIBRARY_PATH', ''), bin_path))
            else:
                bin_path = "timeout -k {} {} {}".format(self.abort_timeout, self.fail_timeout, bin_path)
                self.run("LD_LIBRARY_PATH=%s %s" % (os.environ.get('LD_LIBRARY_PATH', ''), bin_path))
