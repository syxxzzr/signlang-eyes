from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import apply_conandata_patches, copy, rm
from conan.tools.scm import Git


class RockchipMppConan(ConanFile):
    name = "rockchip-mpp"
    package_type = "shared-library"
    license = "Apache-2.0 OR MIT"
    url = "https://github.com/rockchip-linux/mpp"
    homepage = "https://github.com/rockchip-linux/mpp"
    description = "Rockchip Media Process Platform JPEG hardware decoder"
    topics = ("rockchip", "mpp", "mjpeg", "hardware-decoding")
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "patches/*"

    def set_version(self):
        self.version = self.conan_data["version"]

    def _source_data(self):
        return self.conan_data["sources"][str(self.version)]

    def validate(self):
        if str(self.settings.os) != "Linux" or str(self.settings.arch) != "armv8":
            raise ConanInvalidConfiguration("Rockchip MPP is only supported for Linux armv8 in this project")

    def layout(self):
        cmake_layout(self, src_folder="upstream")

    def source(self):
        source_data = self._source_data()
        git = Git(self, folder=self.source_folder)
        git.clone(
            url=source_data["url"],
            target=".",
            args=["--depth", "1", "--branch", source_data["tag"]],
        )
        git.checkout(source_data["commit"])
        apply_conandata_patches(self)

    def generate(self):
        self.conf.define("tools.build:jobs", 4)
        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_TEST"] = False
        toolchain.variables["BUILD_SHARED_LIBS"] = True
        toolchain.variables["CMAKE_INSTALL_LIBDIR"] = "lib"
        disabled_codecs = (
            "ENABLE_AVSD",
            "ENABLE_AVS2D",
            "ENABLE_H263D",
            "ENABLE_H264D",
            "ENABLE_H265D",
            "ENABLE_MPEG2D",
            "ENABLE_MPEG4D",
            "ENABLE_VP8D",
            "ENABLE_VP9D",
            "ENABLE_AV1D",
            "ENABLE_H264E",
            "ENABLE_JPEGE",
            "ENABLE_H265E",
            "ENABLE_VP8E",
        )
        for codec_option in disabled_codecs:
            toolchain.variables[codec_option] = False
        toolchain.variables["ENABLE_JPEGD"] = True
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        self.output.info("Building Rockchip MPP with 4 parallel jobs")
        cmake.build(cli_args=["--parallel", "4"])

    def package(self):
        cmake = CMake(self)
        cmake.install()

        package_root = Path(self.package_folder)
        copy(
            self,
            "*",
            src=Path(self.source_folder) / "LICENSES",
            dst=package_root / "licenses",
        )
        rm(self, "*.a", package_root / "lib")
        rm(self, "librockchip_vpu.so*", package_root / "lib")
        rm(self, "rockchip_vpu.pc", package_root / "lib" / "pkgconfig")

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "rockchip-mpp")
        self.cpp_info.set_property("cmake_target_name", "rockchip-mpp::rockchip_mpp")
        self.cpp_info.set_property("pkg_config_name", "rockchip_mpp")
        self.cpp_info.bindirs = []
        self.cpp_info.includedirs = ["include/rockchip"]
        self.cpp_info.libs = ["rockchip_mpp"]
