# A Conan recipe that builds Kompoz from the repository it is in, with FreeType, HarfBuzz and libpng
# from ConanCenter (on Linux the X11, Wayland and xkbcommon headers its platforms are built with
# from the system's packages):
#   conan create packaging/conan --build=missing -s compiler.cppstd=20
# The package is found with find_package(kompoz), its own CMake package (kompoz::core, kompoz::app,
# kompoz_copy_assets). Its options with_jpeg, with_webp and with_curl (on by default) are AsyncImage's
# libraries on Linux and macOS, from ConanCenter too; Windows decodes and downloads with its own.
import os
import re

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, load
from conan.tools.system.package_manager import Apt

ROOT = os.path.join("..", "..")


class KompozConan(ConanFile):
    name = "kompoz"
    license = "Apache-2.0"
    url = "https://github.com/kasimtmc/kompoz"
    homepage = "https://github.com/kasimtmc/kompoz"
    description = "Jetpack Compose's model and API in C++20: a declarative UI library."
    topics = ("ui", "gui", "compose", "declarative")
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"with_jpeg": [True, False], "with_webp": [True, False], "with_curl": [True, False]}
    default_options = {"with_jpeg": True, "with_webp": True, "with_curl": True}

    def set_version(self):
        cmake = load(self, os.path.join(self.recipe_folder, ROOT, "CMakeLists.txt"))
        self.version = re.search(r"project\(kompoz VERSION ([0-9.]+)", cmake).group(1)

    def export_sources(self):
        for pattern in ("CMakeLists.txt", "LICENSE", "NOTICE", "cmake/*", "include/*", "src/*", "platform/*", "assets/*"):
            copy(self, pattern, os.path.join(self.recipe_folder, ROOT), self.export_sources_folder)

    def requirements(self):
        self.requires("freetype/[>=2.13]")
        self.requires("harfbuzz/[>=8]")
        self.requires("libpng/[>=1.6]")
        if self.settings.os not in ("Macos", "iOS", "Emscripten"):  # the GPU renderer's Vulkan backend
            self.requires("vulkan-headers/[>=1.3]")
        if self.settings.os != "Windows":
            if self.options.with_jpeg:
                self.requires("libjpeg-turbo/[>=2.1]")
            if self.options.with_webp:
                self.requires("libwebp/[>=1.3]")
            if self.options.with_curl:
                self.requires("libcurl/[>=8]")

    def system_requirements(self):
        # Linux: the headers Kompoz's Wayland and X11 platforms are built with (their libraries
        # are loaded at run time).
        Apt(self).install(["libwayland-dev", "libxkbcommon-dev", "libx11-dev", "libxi-dev", "libxcursor-dev", "libxrandr-dev"])

    def configure(self):
        if self.settings.os == "Windows":  # (not used there)
            del self.options.with_jpeg
            del self.options.with_webp
            del self.options.with_curl

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.cache_variables["KOMPOZ_VENDORED_DEPS"] = False
        toolchain.cache_variables["KOMPOZ_INSTALL"] = True
        toolchain.cache_variables["KOMPOZ_BUILD_TESTS"] = False
        toolchain.cache_variables["KOMPOZ_BUILD_EXAMPLES"] = False
        if self.settings.os != "Windows":
            for lib in ("jpeg", "webp", "curl"):
                toolchain.cache_variables[f"KOMPOZ_WITH_{lib.upper()}"] = "ON" if self.options.get_safe(f"with_{lib}") else "OFF"
        # Conan's CMake packages only, never a system library through pkg-config.
        toolchain.cache_variables["CMAKE_DISABLE_FIND_PACKAGE_PkgConfig"] = True
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()

    def package_info(self):
        # find_package(kompoz) uses the package's own kompozConfig.cmake.
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs.append(os.path.join("lib", "cmake", "kompoz"))
