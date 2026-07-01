from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get


class CppPinyinConan(ConanFile):
    name = "cpp-pinyin"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": True, "fPIC": True}

    def set_version(self):
        self.version = self.conan_data["version"]

    def _source_data(self):
        return self.conan_data["sources"][str(self.version)]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def source(self):
        source_data = self._source_data()
        get(self, url=source_data["url"], sha256=source_data["sha256"], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["CPP_PINYIN_BUILD_STATIC"] = not self.options.shared
        tc.variables["CPP_PINYIN_BUILD_TESTS"] = False
        tc.variables["CPP_PINYIN_INSTALL"] = True
        if "fPIC" in self.options:
            tc.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = self.options.fPIC
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "cpp-pinyin")
        self.cpp_info.set_property("cmake_target_name", "cpp-pinyin::cpp-pinyin")
        self.cpp_info.set_property("pkg_config_name", "cpp-pinyin")
        self.cpp_info.libs = ["cpp-pinyin"]
