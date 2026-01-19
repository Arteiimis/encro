add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.check")
set_license("GPL-3.0")

set_languages("c++23")
set_toolchains("clang")

add_cxxflags("-Wno-c++26-extensions")
add_cxxflags("-ftrivial-auto-var-init=pattern")

if is_subhost("windows") then
  add_defines("NOMINMAX")
  add_defines("WIN32_LEAN_AND_MEAN")
end

add_requires("boost[all]")
add_requires("thread-pool")
add_requires("indicators")

target("video_encoder")
  set_kind("binary")

  add_packages("boost", "thread-pool", "indicators")
  add_files("src/*.cpp")
target_end()

includes("./test/xmake.lua")