add_requires("catch2")

target("tests")
  set_kind("binary")

  add_packages("boost", "thread-pool", "indicators", "catch2")
  add_includedirs("../src")
  add_files("*.cpp", "../src/*.cpp|main.cpp")
target_end()