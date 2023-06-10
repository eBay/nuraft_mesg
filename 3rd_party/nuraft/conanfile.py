from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import patch, copy, get
import os

required_conan_version = ">=1.53.0"


class NuRaftConan(ConanFile):
    name = "nuraft"
    homepage = "https://github.corp.ebay.com/sds/NuRaft"
    description = """Cornerstone based RAFT library."""
    topics = ("raft",)
    url = "https://github.com/conan-io/conan-center-index"
    license = "Apache-2.0"
    version = "2.2.0"

    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "asio": ["boost", "standalone"],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "asio": "boost",
    }

    exports_sources = "patches/*"

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        self.requires("openssl/1.1.1q")
        if self.options.asio == "boost":
            self.requires("boost/1.79.0")
        else:
            self.requires("asio/1.27.0")

    def validate(self):
        if self.settings.os == "Windows":
            raise ConanInvalidConfiguration(f"{self.ref} doesn't support Windows")
        if self.settings.os == "Macos" and self.options.shared:
            raise ConanInvalidConfiguration(f"{self.ref} shared not supported for Macos")
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, 11)

    def source(self):
        get(self, "https://github.com/eBay/nuraft/archive/62d73b52b6897d4ec1c02d77fe5f7909705399be.tar.gz", strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        patch(self, patch_file="patches/patch.diff")
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["nuraft"]
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs.extend(["m", "pthread"])
