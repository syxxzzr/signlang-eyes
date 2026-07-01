from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get, save
from pathlib import Path


class Iceoryx2Conan(ConanFile):
    name = "iceoryx2"
    package_type = "shared-library"
    settings = "os", "arch", "compiler", "build_type"

    def set_version(self):
        self.version = self.conan_data["version"]

    def _source_data(self):
        return self.conan_data["sources"][str(self.version)]

    def _rust_target_triplet(self):
        if str(self.settings.arch) == "armv8":
            return "aarch64-unknown-linux-gnu"
        if str(self.settings.arch) in ("x86_64", "amd64"):
            return ""
        raise ConanInvalidConfiguration(f"Unsupported iceoryx2 Rust target for arch={self.settings.arch}")

    def _target_env_name(self, rust_target_triplet):
        return rust_target_triplet.upper().replace("-", "_")

    def validate(self):
        if str(self.settings.os) != "Linux":
            raise ConanInvalidConfiguration("This iceoryx2 recipe is only configured for Linux")

    def layout(self):
        cmake_layout(self)

    def source(self):
        source_data = self._source_data()
        get(self, url=source_data["url"], sha256=source_data["sha256"], strip_root=True)

    def generate(self):
        rust_target_triplet = self._rust_target_triplet()

        tc = CMakeToolchain(self)
        tc.variables["BUILD_CXX"] = True
        tc.variables["BUILD_EXAMPLES"] = False
        tc.variables["BUILD_TESTING"] = False
        if rust_target_triplet:
            tc.variables["RUST_TARGET_TRIPLET"] = rust_target_triplet
        tc.generate()

        if rust_target_triplet:
            self._generate_cargo_config(rust_target_triplet)

    def _generate_cargo_config(self, rust_target_triplet):
        compiler_executables = self.conf.get("tools.build:compiler_executables", default={})
        c_compiler = compiler_executables.get("c")
        if not c_compiler:
            raise ConanInvalidConfiguration(
                "Cross compiling iceoryx2 requires tools.build:compiler_executables['c'] for Cargo"
            )

        sysroot = self.conf.get("tools.build:sysroot", default="")
        target_env_name = self._target_env_name(rust_target_triplet)
        cargo_config = [
            "[build]",
            'rustdocflags = ["-D", "warnings"]',
            "",
            "[env]",
            'RUST_TEST_THREADS = "1"',
            'RUST_BACKTRACE = "1"',
            f'CC_{target_env_name} = "{c_compiler}"',
            f'CARGO_TARGET_{target_env_name}_LINKER = "{c_compiler}"',
            "",
            f"[target.{rust_target_triplet}]",
            f'linker = "{c_compiler}"',
        ]
        if sysroot:
            cargo_config.append(f'rustflags = ["-C", "link-arg=--sysroot={sysroot}"]')

        save(self, Path(self.source_folder) / ".cargo" / "config.toml", "\n".join(cargo_config) + "\n")

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        lib_dir = Path(self.package_folder) / "lib"

        self.cpp_info.set_property("cmake_find_mode", "config")
        self.cpp_info.set_property("cmake_file_name", "iceoryx2-cxx")
        self.cpp_info.set_property("cmake_target_name", "iceoryx2-cxx::shared-lib-cxx")
        self.cpp_info.bindirs = []
        self.cpp_info.includedirs = [f"include/iceoryx2/v{self.version}"]
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
