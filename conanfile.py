from os.path import join
from conan import ConanFile
from conan.tools.files import copy
from conan.tools.build import check_min_cppstd
from conans import CMake

required_conan_version = ">=1.50.0"

class NuRaftMesgConan(ConanFile):
    name = "nuraft_mesg"
    version = "0.0.10"

    homepage = "https://github.com/eBay/nuraft_mesg"
    description = "A gRPC service for NuRAFT"
    topics = ("ebay", "nublox", "raft")
    url = "https://github.com/eBay/nuraft_mesg"
    license = "Apache-2.0"

    settings = "arch", "os", "compiler", "build_type"

    options = {
                "shared": ['True', 'False'],
                "fPIC": ['True', 'False'],
                "sanitize": ['True', 'False'],
                "testing": ['True', 'False'],
                }
    default_options = {
                'shared': False,
                'fPIC': True,
                'sanitize': False,
                'testing': True,
                'sisl:prerelease': False,
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
        if self.settings.build_type == "Debug" and self.options.sanitize:
            self.options['sisl'].malloc_impl = 'libc'

    def build_requirements(self):
        self.build_requires("gtest/1.13.0")
        if (self.options.testing):
            self.build_requires("jungle/cci.20221201")

    def requirements(self):
        self.requires("sisl/[~=9,           include_prerelease=True]@oss/master")
        self.requires("nuraft/2.2.0")

        self.requires("boost/1.79.0", override=True)
        self.requires("openssl/1.1.1q", override=True)
        self.requires("lz4/1.9.4", override=True)

    def validate(self):
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, 17)

    def build(self):
        cmake = CMake(self)

        definitions = {'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'CONAN_CMAKE_SILENT_OUTPUT': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF'}

        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                definitions['MEMORY_SANITIZER_ON'] = 'ON'

        cmake.configure(defs=definitions)
        cmake.build()
        if (self.options.testing):
            cmake.test(output_on_failure=True)

    def package(self):
        lib_dir = join(self.package_folder, "lib")
        copy(self, "LICENSE", self.source_folder, join(self.package_folder, "licenses"), keep_path=False)
        copy(self, "*.h*", join(self.source_folder, "src", "include"), join(self.package_folder, "include", "nuraft_mesg"), keep_path=True)
        copy(self, "*.lib", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.a", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dylib*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dll*", self.build_folder, join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.proto", join(self.source_folder, "src", "proto"), join(self.package_folder, "proto"), keep_path=False)
        gen_dir = join(self.package_folder, "include", "nuraft_mesg", "proto")
        copy(self, "*.pb.h", join(self.build_folder, "src"), gen_dir, keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["nuraft_mesg"]
        if self.settings.build_type == "Debug" and self.options.sanitize:
            self.cpp_info.sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.exelinkflags.append("-fsanitize=address")
            self.cpp_info.sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.exelinkflags.append("-fsanitize=undefined")
