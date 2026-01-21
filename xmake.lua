add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.check", "mode.minsizerel")
set_license("GPL-3.0")

set_languages("c++23")
set_toolchains("clang")
set_toolset("ld", "lld-link")

add_cxxflags("-Wno-c++26-extensions")
add_cxxflags("-ftrivial-auto-var-init=pattern")

if is_subhost("windows") then
  add_defines("NOMINMAX")
  add_defines("WIN32_LEAN_AND_MEAN")
end

add_requires("boost[all]")
add_requires("thread-pool")
add_requires("spdlog[std_format]")
add_requires("indicators")
add_requires("catch2")

target("video_encoder")
  set_kind("binary")
  set_policy("build.optimization.lto", true)

  add_packages("boost", "thread-pool", "indicators", "spdlog")
  add_includedirs("src")
  add_files("src/*.cpp")
target_end()


target("tests")
  set_kind("binary")
  set_default(false)
  set_policy("build.optimization.lto", true)

  add_packages("boost", "thread-pool", "indicators", "spdlog", "catch2")
  add_includedirs("src")
  add_files("test/*.cpp", "src/*.cpp|main.cpp")
target_end()