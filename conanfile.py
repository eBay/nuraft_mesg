from os.path import join
from conan import ConanFile
from conan.tools.files import copy
from conan.tools.build import check_min_cppstd
from conans import CMake

required_conan_version = ">=1.50.0"

class NuRaftMesgConan(ConanFile):
    name = "nuraft_mesg"
    version = "2.3.1"

    homepage = "https://github.com/eBay/nuraft_mesg"
    description = "A gRPC service for NuRAFT"
    topics = ("ebay", "nublox", "raft")
    url = "https://github.com/eBay/nuraft_mesg"
    license = "Apache-2.0"

    settings = "arch", "os", "compiler", "build_type"

    options = {
                "shared": ['True', 'False'],
                "fPIC": ['True', 'False'],
                "coverage": ['True', 'False'],
                "sanitize": ['True', 'False'],
                "testing": ['True', 'False'],
                }
    default_options = {
                'shared': False,
                'fPIC': True,
                'coverage': False,
                'sanitize': False,
                'testing': True,
            }

    generators = "cmake", "cmake_find_package"
    exports = ["LICENSE"]
    exports_sources = (
                        "CMakeLists.txt",
                        "cmake/*",
                        "include/*",
                        "src/*",
                        )

    def configure(self):
        if self.options.shared:
            del self.options.fPIC
        if self.settings.build_type == "Debug":
            if self.options.coverage and self.options.sanitize:
                raise ConanInvalidConfiguration("Sanitizer does not work with Code Coverage!")
            if not self.options.testing:
                if self.options.coverage or self.options.sanitize:
                    raise ConanInvalidConfiguration("Coverage/Sanitizer requires Testing!")

    def build_requirements(self):
        self.build_requires("gtest/1.14.0")
        if (self.options.testing):
            self.build_requires("jungle/cci.20221201")

    def requirements(self):
        self.requires("sisl/[~=11, include_prerelease=True]@oss/master")
        self.requires("nuraft/2.3.0")

        self.requires("boost/[>=1.80]")
        self.requires("flatbuffers/23.5.26")
        self.requires("openssl/3.1.3")

    def validate(self):
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, 17)

    def build(self):
        cmake = CMake(self)

        definitions = {'CONAN_BUILD_COVERAGE': 'OFF',
                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON',
                       'CONAN_CMAKE_SILENT_OUTPUT': 'ON',
                       'MEMORY_SANITIZER_ON': 'OFF'}

        if self.settings.build_type == "Debug":
            if self.options.sanitize:
                definitions['MEMORY_SANITIZER_ON'] = 'ON'
            elif self.options.coverage:
                definitions['CONAN_BUILD_COVERAGE'] = 'ON'

        cmake.configure(defs=definitions)
        cmake.build()
        if (self.options.testing):
            cmake.test(output_on_failure=True)

    def package(self):
        lib_dir = join(self.package_folder, "lib")
        copy(self, "LICENSE", self.source_folder, join(self.package_folder, "licenses"), keep_path=False)
        copy(self, "*.h*", join(self.source_folder, "include"), join(self.package_folder, "include"), keep_path=True)
        copy(self, "*.lib", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.a", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dylib*", self.build_folder, lib_dir, keep_path=False)
        copy(self, "*.dll*", self.build_folder, join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so*", self.build_folder, lib_dir, keep_path=False)

    def package_info(self):
        self.cpp_info.names["cmake_find_package"] = "NuraftMesg"
        self.cpp_info.names["cmake_find_package_multi"] = "NuraftMesg"
        self.cpp_info.components["proto"].libs = ["nuraft_mesg", "nuraft_mesg_proto"]
        self.cpp_info.components["proto"].requires = ["nuraft::nuraft", "sisl::sisl"]

        if self.settings.build_type == "Debug" and self.options.sanitize:
            self.cpp_info.components["proto"].sharedlinkflags.append("-fsanitize=address")
            self.cpp_info.components["proto"].exelinkflags.append("-fsanitize=address")
            self.cpp_info.components["proto"].sharedlinkflags.append("-fsanitize=undefined")
            self.cpp_info.components["proto"].exelinkflags.append("-fsanitize=undefined")
