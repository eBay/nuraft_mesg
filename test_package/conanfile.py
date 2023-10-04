from conans import ConanFile
from conan.tools.build import cross_building
from conans import CMake
import os

class TestPackageConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "cmake_find_package"

    def build(self):
        cmake = CMake(self)
        cmake.configure(defs={'CONAN_CMAKE_SILENT_OUTPUT': 'ON'})
        cmake.build()

    def test(self):
        if not cross_building(self):
            sbin_path = os.path.join("bin", "example_server")
            cbin_path = os.path.join("bin", "example_client")
            self.run(sbin_path, run_environment=True)
            self.run(cbin_path, run_environment=True)
