from setuptools import setup, Extension

module = Extension(
    "keyer_simulator_native",
    sources=["keyer_simulator.cpp"],
    extra_compile_args=["-std=c++20", "-Ofast"],
)

setup(
    name="keyer_simulator_native",
    version="1.0",
    description="Native C++ keyer simulator",
    ext_modules=[module],
)
