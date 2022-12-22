import os
from conans import ConanFile, CMake, tools

class NuRaftGRPCConan(ConanFile):
    name = "nuraft_grpc"
    version = "5.4.2"
    homepage = "https://github.com/eBay/nuraft_grpc"
    description = "A gRPC service for nuraft"
    topics = ("ebay", "nublox", "raft")
    url = "https://github.com/eBay/nuraft_grpc"
    license = "Apache-2.0"

    settings = "arch", "os", "compiler", "build_type"
    options = {
                "shared": ['True', 'False'],
                "fPIC": ['True', 'False'],
                "sanitize": ['True', 'False'],
                }
    default_options = {
                'shared': False,
                'fPIC': True,
                'sanitize': False,
                'sisl:prerelease': True,
            }

    generators = "cmake", "cmake_find_package"
    exports = ["LICENSE"]
    exports_sources = (
                        "CMakeLists.txt",
                        "cmake/*",
                        "src/*",
                        )

    def config_options(self):
        if self.settings.build_type != "Debug":
            del self.options.sanitize

    def configure(self):
        if self.options.shared:
            del self.options.fPIC

    def requirements(self):
        self.requires("nuraft/nbi.2.0.0")
        self.requires("openssl/1.1.1s")
        self.requires("sisl/[~=8.3, include_prerelease=True]@oss/master")

    def build(self):
        cmake = CMake(self)

        definitions = {'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF'}
        test_target = None

        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                definitions['MEMORY_SANITIZER_ON'] = 'ON'

        cmake.configure(defs=definitions)
        cmake.build()
        cmake.test(target=test_target)

    def package(self):
        self.copy(pattern="LICENSE", dst="licenses")
        self.copy("*.h", dst="include/nuraft_grpc", excludes="*.pb.h", keep_path=False)
        self.copy("*.pb.h", dst="include/nuraft_grpc/proto", keep_path=False)
        self.copy("*.hpp", dst="include/nuraft_grpc", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.dylib*", dst="lib", keep_path=False, symlinks=True)
        self.copy("*.so", dst="lib", keep_path=False, symlinks=True)
        self.copy("*.a", dst="lib", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.proto", dst="proto/", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nuraft_grpc"]
        if self.settings.build_type == "Debug" and self.options.sanitize:
            self.cpp_info.sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.exelinkflags.append("-fsanitize=address")
            self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.exelinkflags.append("-fsanitize=undefined")
