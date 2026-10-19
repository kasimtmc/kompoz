# Builds tests/package (the README's first example) against the package and runs it offscreen.
import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.env import Environment


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
            # tests/package is built as this build's package/ folder; multi-config generators
            # (Visual Studio) put its demo in a folder of the build type under it.
            demo = os.path.join("package", self.cpp.build.bindir, "demo")
            env = Environment()
            env.define("SDL_VIDEO_DRIVER", "offscreen")
            env.define("SDL_RENDER_DRIVER", "software")
            env.define("KOMPOZ_SCRIPT", "wait 5;click 70 70;wait 5;quit")
            with env.vars(self, scope="run").apply():
                self.run(demo, env="conanrun")
