import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy, get
from conan.tools.layout import basic_layout


class ChronicleConan(ConanFile):
    name = "chronicle"
    version = "2.1.2"
    description = (
        "Time travel for runtime state: a C++23 header-only library for "
        "recording, querying, and replaying the history of tracked "
        "in-process state."
    )
    homepage = "https://github.com/bhouvana/Chronicle"
    url = "https://github.com/bhouvana/Chronicle"
    license = "Apache-2.0"
    topics = ("time-travel", "state-history", "debugging", "observability", "header-only")

    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    def layout(self):
        basic_layout(self, src_folder="src")

    def source(self):
        get(
            self,
            f"https://github.com/bhouvana/Chronicle/archive/refs/tags/v{self.version}.tar.gz",
            strip_root=True,
        )

    def package_id(self):
        self.info.clear()

    def validate(self):
        check_min_cppstd(self, 23)

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        copy(
            self,
            "*.hpp",
            src=os.path.join(self.source_folder, "include"),
            dst=os.path.join(self.package_folder, "include"),
        )

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "chronicle")
        self.cpp_info.set_property("cmake_target_name", "chronicle::core")
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs.append("pthread")
