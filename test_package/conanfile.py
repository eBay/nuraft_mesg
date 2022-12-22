from conans import ConanFile, CMake, tools, RunEnvironment
import os

class TestPackageConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "cmake_find_package"

    fail_timeout = '30s'
    abort_timeout = '35s'

    def requirements(self):
        self.requires("nlohmann_json/3.10.5")
        self.requires("jungle_logstore/nbi.20221129@sds/master")
        self.requires("lz4/1.9.4", override=True)

        self.requires("nuraft/2.0.0", override=True)

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
