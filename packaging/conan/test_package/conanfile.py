# Builds tests/package (the README's first example) against the package and runs it offscreen.
import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class KompozTestConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            demo = os.path.join(self.cpp.build.bindir, "package", "demo")
            self.run(f'SDL_VIDEO_DRIVER=offscreen SDL_RENDER_DRIVER=software KOMPOZ_SCRIPT="wait 5;click 70 70;wait 5;quit" {demo}',
                     env="conanrun")
