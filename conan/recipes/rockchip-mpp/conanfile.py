from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, replace_in_file, rm
from conan.tools.scm import Git


class RockchipMppConan(ConanFile):
    name = "rockchip-mpp"
    package_type = "shared-library"
    license = "Apache-2.0 OR MIT"
    homepage = "https://github.com/rockchip-linux/mpp"
    url = "https://github.com/rockchip-linux/mpp"
    description = "Rockchip Media Process Platform"
    topics = ("rockchip", "media", "video", "codec", "hardware-acceleration")

    settings = "os", "arch", "compiler", "build_type"

    def set_version(self):
        self.version = self.conan_data["version"]

    def _source_data(self):
        return self.conan_data["sources"][str(self.version)]

    def validate(self):
        if str(self.settings.os) != "Linux" or str(self.settings.arch) != "armv8":
            raise ConanInvalidConfiguration(
                "Rockchip MPP is packaged for Linux armv8 targets only"
            )

    def layout(self):
        cmake_layout(self)

    def source(self):
        source_data = self._source_data()
        git = Git(self, folder=self.source_folder)
        git.run("init")
        git.run(f"remote add origin {source_data['url']}")
        git.run(f"fetch --depth 1 origin {source_data['commit']}")
        git.run("checkout --detach FETCH_HEAD")

    def generate(self):
        toolchain = CMakeToolchain(self)
        # MPP intentionally uses GNU extensions such as `typeof` in public
        # compatibility headers. Keep the requested language level while using
        # the GNU dialect expected by the upstream AArch64 build.
        toolchain.blocks["cppstd"].values["cppstd_extensions"] = "ON"
        toolchain.variables["BUILD_TEST"] = False
        toolchain.variables["BUILD_SHARED_LIBS"] = True
        toolchain.variables["WARNINGS_AS_ERRORS"] = False
        toolchain.variables["ASAN_CHECK"] = False
        toolchain.variables["CMAKE_INSTALL_INCLUDEDIR"] = "include"
        toolchain.variables["CMAKE_INSTALL_LIBDIR"] = "lib"
        toolchain.generate()

    def build(self):
        # EmbedFire's build/linux/aarch64/make-Makefiles.bash configures this
        # same top-level CMake project. Conan supplies the equivalent AArch64
        # compiler, sysroot, build type, and install prefix through its toolchain.
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

        package_root = Path(self.package_folder)
        copy(
            self,
            pattern="*",
            src=Path(self.source_folder) / "LICENSES",
            dst=package_root / "licenses",
        )

        # Upstream installs shared and static MPP variants unconditionally.
        # This package exposes the shared ABI, so omit the duplicate archive.
        rm(self, pattern="*.a", folder=package_root / "lib")

        # Conan may relocate a package after installation. Keep upstream's
        # pkg-config metadata relative to the final package directory.
        package_prefix = str(package_root.as_posix())
        for name in ("rockchip_mpp.pc", "rockchip_vpu.pc"):
            pc_file = package_root / "lib" / "pkgconfig" / name
            replace_in_file(
                self,
                pc_file,
                f"prefix={package_prefix}",
                "prefix=${pcfiledir}/../..",
            )

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "rockchip-mpp")

        mpp = self.cpp_info.components["mpp"]
        mpp.set_property("cmake_target_name", "rockchip-mpp::rockchip_mpp")
        mpp.set_property("pkg_config_name", "rockchip_mpp")
        mpp.includedirs = ["include/rockchip"]
        mpp.libdirs = ["lib"]
        mpp.bindirs = []
        mpp.libs = ["rockchip_mpp"]
        mpp.system_libs = ["pthread", "m"]

        vpu = self.cpp_info.components["vpu"]
        vpu.set_property("cmake_target_name", "rockchip-mpp::rockchip_vpu")
        vpu.set_property("pkg_config_name", "rockchip_vpu")
        vpu.includedirs = ["include/rockchip"]
        vpu.libdirs = ["lib"]
        vpu.bindirs = []
        vpu.libs = ["rockchip_vpu"]
        vpu.system_libs = ["dl"]
        vpu.requires = ["mpp"]
