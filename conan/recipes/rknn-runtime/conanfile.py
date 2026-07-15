from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from pathlib import Path


class RknnRuntimeConan(ConanFile):
    name = "rknn-runtime"
    version = "2.3.2"
    package_type = "shared-library"
    settings = "os", "arch"
    exports_sources = "vendor/*"

    def validate(self):
        if str(self.settings.os) != "Linux" or str(self.settings.arch) != "armv8":
            raise ConanInvalidConfiguration("This prebuilt RKNN runtime package is only available for Linux armv8")

    def package(self):
        source_root = Path(self.source_folder) / "vendor"
        if not source_root.exists():
            raise ConanInvalidConfiguration(f"RKNN runtime prebuilt root not found: {source_root}")

        copy(self, "*", src=source_root / "include", dst=Path(self.package_folder) / "include")
        copy(self, "librknnrt.so*", src=source_root / "lib" / "aarch64", dst=Path(self.package_folder) / "lib")

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "rknn-runtime")
        self.cpp_info.set_property("cmake_target_name", "signlang::rknn_runtime")
        self.cpp_info.bindirs = []
        self.cpp_info.libs = ["rknnrt"]

        headers = self.cpp_info.components["headers"]
        headers.set_property("cmake_target_name", "signlang::rknn_headers")
        headers.includedirs = ["include"]
        headers.libdirs = []

        runtime = self.cpp_info.components["runtime"]
        runtime.set_property("cmake_target_name", "signlang::rknn_runtime")
        runtime.libs = ["rknnrt"]
        runtime.requires = ["headers"]
