add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.check", "mode.minsizerel")
set_policy("build.optimization.lto", true)

set_languages("c++23")
set_toolchains("clang")

add_cxxflags("-Wno-c++26-extensions")
add_cxxflags("-ftrivial-auto-var-init=pattern")

if is_plat("windows") then
  add_defines("NOMINMAX")
  add_defines("WIN32_LEAN_AND_MEAN")
  add_defines("_MSVC_STL_HARDENING=1")
end

add_requires("boost[all]")
add_requires("thread-pool")
add_requires("spdlog[std_format]")
add_requires("indicators")
if is_plat("windows") then
  add_requires("libzippp[toolchains=clang-cl]")
else
  add_requires("libzippp")
end
add_requires("catch2")

target("video_encoder")
  set_kind("binary")

  add_packages("boost", "thread-pool", "indicators", "spdlog", "libzippp")

  add_includedirs("src")
  add_files("src/*.cpp")
target_end()

target("tests")
  set_kind("binary")
  set_default(false)

  add_deps("video_encoder")
  add_packages("catch2", "boost", "thread-pool", "indicators", "spdlog", "libzippp")
  add_includedirs("src")
  add_files("tests/*.cpp")
  add_files("src/*.cpp|main.cpp")
target_end()