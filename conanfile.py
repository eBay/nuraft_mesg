from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake
from conan.tools.files import copy
from conan.tools.files import copy
from os.path import join

required_conan_version = ">=1.60.0"

class NuRaftMesgConan(ConanFile):
    name = "nuraft_mesg"
    version = "3.7.6"

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
                }
    default_options = {
                'shared': False,
                'fPIC': True,
                'coverage': False,
                'sanitize': False,
            }

    exports_sources = (
                        "LICENSE",
                        "CMakeLists.txt",
                        "cmake/*",
                        "include/*",
                        "src/*",
                        )

    def _min_cppstd(self):
        return 20

    def validate(self):
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, self._min_cppstd())

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        if self.settings.build_type == "Debug":
            if self.options.coverage and self.options.sanitize:
                raise ConanInvalidConfiguration("Sanitizer does not work with Code Coverage!")
            if self.conf.get("tools.build:skip_test", default=False):
                if self.options.coverage or self.options.sanitize:
                    raise ConanInvalidConfiguration("Coverage/Sanitizer requires Testing!")

    def build_requirements(self):
        self.test_requires("lz4/[>=1.9]")
        self.test_requires("gtest/1.14.0")
        self.test_requires("jungle/cci.20221201")

    def requirements(self):
        self.requires("boost/1.83.0", transitive_headers=True)
        self.requires("sisl/[>=12.3.3]@oss/master", transitive_headers=True)
        self.requires("nuraft/2.4.5", transitive_headers=True)

    def layout(self):
        self.folders.source = "."
        self.folders.build = join("build", str(self.settings.build_type))
        self.folders.generators = join(self.folders.build, "generators")

        self.cpp.source.includedirs = ["include"]

        self.cpp.build.components["proto"].libdirs = ["src/proto"]

        self.cpp.package.components["proto"].libs = ["nuraft_mesg_proto"]
        self.cpp.package.components["proto"].set_property("pkg_config_name", "libnuraft_mesg_proto")
        self.cpp.package.includedirs = ["include"] # includedirs is already set to 'include' by
        self.cpp.package.libdirs = ["lib"]

    def generate(self):
        # This generates "conan_toolchain.cmake" in self.generators_folder
        tc = CMakeToolchain(self)
        tc.variables["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON"
        tc.variables["CONAN_CMAKE_SILENT_OUTPUT"] = "ON"
        tc.variables["CTEST_OUTPUT_ON_FAILURE"] = "ON"
        tc.variables["PACKAGE_VERSION"] = self.version
        if self.settings.build_type == "Debug":
            if self.options.get_safe("coverage"):
                tc.variables['BUILD_COVERAGE'] = 'ON'
            elif self.options.get_safe("sanitize"):
                tc.variables['MEMORY_SANITIZER_ON'] = 'ON'
        tc.generate()

        # This generates "boost-config.cmake" and "grpc-config.cmake" etc in self.generators_folder
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if not self.conf.get("tools.build:skip_test", default=False):
            cmake.test()

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
        self.cpp_info.components["proto"].requires.extend([
            "nuraft::nuraft",
            "boost::boost",
            "sisl::sisl"
            ])

        for component in self.cpp_info.components.values():
            if  self.options.get_safe("sanitize"):
                component.sharedlinkflags.append("-fsanitize=address")
                component.exelinkflags.append("-fsanitize=address")
                component.sharedlinkflags.append("-fsanitize=undefined")
                component.exelinkflags.append("-fsanitize=undefined")

        self.cpp_info.set_property("cmake_file_name", "NuraftMesg")
        self.cpp_info.set_property("cmake_target_name", "NuraftMesg::NuraftMesg")
        self.cpp_info.names["cmake_find_package"] = "NuraftMesg"
        self.cpp_info.names["cmake_find_package_multi"] = "NuraftMesg"
