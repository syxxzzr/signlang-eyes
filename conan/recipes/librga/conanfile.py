from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from pathlib import Path


class LibRgaConan(ConanFile):
    name = "librga"
    version = "1.0.0"
    package_type = "shared-library"
    settings = "os", "arch"
    exports_sources = "vendor/*"

    def validate(self):
        if str(self.settings.os) != "Linux" or str(self.settings.arch) != "armv8":
            raise ConanInvalidConfiguration("This prebuilt librga package is only available for Linux armv8")

    def package(self):
        source_root = Path(self.source_folder) / "vendor"
        if not source_root.exists():
            raise ConanInvalidConfiguration(f"librga prebuilt root not found: {source_root}")

        copy(self, "*", src=source_root / "include", dst=Path(self.package_folder) / "include")
        copy(self, "COPYING", src=source_root, dst=Path(self.package_folder) / "licenses")
        copy(self, "librga.so*", src=source_root / "lib" / "aarch64", dst=Path(self.package_folder) / "lib")
        copy(self, "librga.a", src=source_root / "lib" / "aarch64", dst=Path(self.package_folder) / "lib")

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "librga")
        self.cpp_info.set_property("cmake_target_name", "librga::rga")
        self.cpp_info.bindirs = []
        self.cpp_info.libs = ["rga"]
