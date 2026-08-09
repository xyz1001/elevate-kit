from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps
from conan.tools.scm import Git
from conan.tools.files import update_conandata, copy
import os


class ElevateKitConan(ConanFile):
    name = "elevate-kit"
    version = "1.0.0"
    user = "xyz1001"
    package_type = "library"
    license = "MIT"
    author = "xyz1001 zgzf1001@gmail.com"
    url = "https://github.com/xyz1001/elevate-kit"
    description = "Seamless, Zero-Prompt & Kernel-Verified Cross-Platform Privilege Escalation Framework"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "test": [True, False], "example": [True, False]}
    default_options = {"shared": True, "test": False, "example": False}

    @property
    def version_name(self):
        type_dict = {"stable": "R", "snapshot": "D", "testing": "T", None: "T"}
        type = type_dict.get(self.channel, "S")
        hash = os.getenv("GIT_COMMIT", "")[:6]
        return "%s.%s-%s" % (type, self.version, hash if hash else "unknown")

    def export(self):
        git = Git(self, self.recipe_folder)
        scm_url, scm_commit = git.get_url_and_commit()
        update_conandata(self, {"sources": {"commit": scm_commit, "url": scm_url}})

    def source(self):
        git = Git(self)
        sources = self.conan_data["sources"]
        git.clone(url=sources["url"], target=".")
        git.checkout(commit=sources["commit"])

    def requirements(self):
        self.requires("cli11/2.6.0")
        self.requires("fmt/10.2.1")
        self.requires("nlohmann_json/3.11.3")
        self.requires("scope-lite/0.2.0")
        self.requires("sole/1.0.4")
        if self.options.test:
            self.test_requires("doctest/2.4.11")

    def configure(self):
        pass

    def layout(self):
        cmake_layout(self, generator="Ninja Multi-Config")
        self.cpp.build.bindirs = ["install/bin"]
        self.cpp.build.libdirs = ["install/lib"]
        self.cpp.build.includedirs = ["install/include"]
        self.cpp.source.includedirs.clear()

    def generate(self):
        tc = CMakeToolchain(self, generator="Ninja Multi-Config")
        tc.cache_variables["CMAKE_DEFAULT_BUILD_TYPE"] = str(self.settings.build_type)

        if self.version:
            tc.variables["VERSION_NAME"] = self.version_name
        tc.variables["BUILD_TEST"] = self.options.test
        tc.variables["BUILD_EXAMPLE"] = self.options.example
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

        deploy = self.options.test or self.options.example
        if deploy:
            import_folder = os.path.join(self.build_folder, "import")
            for dep in self.dependencies.values():
                if self.settings.os == "Windows":
                    for bindir in dep.cpp_info.bindirs:
                        copy(self, "*.dll", bindir, os.path.join(import_folder, "bin"))
                        copy(self, "*.pdb", bindir, os.path.join(import_folder, "bin"))
                elif self.settings.os == "Linux" or self.settings.os == "Macos":
                    for libdir in dep.cpp_info.libdirs:
                        copy(self, "*.so*", libdir, os.path.join(import_folder, "lib"))
                        copy(self, "*.dylib", libdir, os.path.join(import_folder, "lib"))

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["elevate_kit"]

        if self.options.shared:
            self.cpp_info.defines = ["ELEVATE_KIT_DLL"]
