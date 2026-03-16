from conan import ConanFile

class Application(ConanFile):
    settings = "os", "compiler", "arch", "build_type"
    generators = "PkgConfigDeps"

    requires = (
        "onnxruntime/1.18.1@aurora",
    )
