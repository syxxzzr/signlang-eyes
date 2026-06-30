from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy
from pathlib import Path


class Iceoryx2Conan(ConanFile):
    name = "iceoryx2"
    version = "0.9.999"
    package_type = "shared-library"
    settings = "os", "arch"
    exports_sources = "vendor/*"

    def validate(self):
        if str(self.settings.os) != "Linux" or str(self.settings.arch) != "armv8":
            raise ConanInvalidConfiguration("This prebuilt iceoryx2 package is only available for Linux armv8")

    def package(self):
        source_root = Path(self.source_folder) / "vendor"
        if not source_root.exists():
            raise ConanInvalidConfiguration(f"iceoryx2 prebuilt root not found: {source_root}")

        copy(self, "*", src=source_root / "include", dst=Path(self.package_folder) / "include")
        copy(self, "*", src=source_root / "lib", dst=Path(self.package_folder) / "lib")
        copy(self, "*", src=source_root / "share", dst=Path(self.package_folder) / "share")

    def package_info(self):
        lib_dir = Path(self.package_folder) / "lib"

        self.cpp_info.set_property("cmake_find_mode", "config")
        self.cpp_info.set_property("cmake_file_name", "iceoryx2-cxx")
        self.cpp_info.set_property("cmake_target_name", "iceoryx2-cxx::shared-lib-cxx")
        self.cpp_info.bindirs = []
        self.cpp_info.includedirs = ["include/iceoryx2/v0.9.999"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.libs = ["iceoryx2_cxx", "iceoryx2_ffi_c"]
        self.cpp_info.system_libs = ["dl", "rt", "m", "pthread"]
        self.cpp_info.exelinkflags = [f"-Wl,-rpath-link,{lib_dir}"]
        self.cpp_info.sharedlinkflags = [f"-Wl,-rpath-link,{lib_dir}"]
        self.cpp_info.builddirs = [
            "lib/cmake/iceoryx2-bb-cxx",
            "lib/cmake/iceoryx2-c",
            "lib/cmake/iceoryx2-cmake-modules",
            "lib/cmake/iceoryx2-cxx",
        ]
