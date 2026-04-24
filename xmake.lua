add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")
set_policy("build.optimization.lto", true)
set_version("0.1.5")

set_languages("c++26")
set_toolchains("clang-cl")

add_cxxflags("-ftrivial-auto-var-init=pattern")

add_plugindirs("./plugins/")

if is_mode("coverage") then
  set_policy("build.optimization.lto", false)
  add_cxxflags("-fprofile-instr-generate", "-fcoverage-mapping")
  add_ldflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
end

if is_plat("windows") then
  add_defines("NOMINMAX")
  add_defines("WIN32_LEAN_AND_MEAN")
  add_defines("_MSVC_STL_HARDENING=1")
end

add_requires("boost[all]")
add_requires("thread-pool")
add_requires("spdlog[fmt_external]")
add_requires("fmt")
add_requires("indicators")
add_requires("immer")
if is_plat("windows") then
  add_requires("libzippp[toolchains=clang-cl]")
else
  add_requires("libzippp")
end
add_requires("catch2")

target("encro")
  set_kind("binary")

  add_packages("boost", "thread-pool", "indicators", "libzippp", "immer", "fmt", "spdlog")
  if is_plat("windows") then
    add_syslinks("dbghelp")
  else
    add_syslinks("dl")
  end

  add_includedirs("src", {public = true})
  add_files("src/**.cpp")
target_end()

target("encro_e2e_tool")
  set_kind("binary")
  set_default(false)

  add_files("tests/e2e/fake_media_tool.cpp")
target_end()

target("tests")
  set_kind("binary")
  set_default(false)

  add_packages("catch2", "boost", "thread-pool", "indicators", "fmt", "spdlog", "libzippp", "immer")
  if is_plat("windows") then
    add_syslinks("dbghelp")
  else
    add_syslinks("dl")
  end
  add_includedirs("src", "tests")
  add_files("tests/*.cpp")
  add_files("tests/app/*.cpp")
  add_files("tests/infra/*.cpp")
  add_files("tests/picture/*.cpp")
  add_files("tests/video/*.cpp")
  add_files("src/**.cpp|main.cpp")
target_end()

target("e2e_tests")
  set_kind("binary")
  set_default(false)

  add_packages("catch2", "boost", "libzippp", "immer", "fmt", "spdlog")
  add_includedirs("tests")
  if is_plat("windows") then
    add_syslinks("dbghelp")
  else
    add_syslinks("dl")
  end

  add_deps("encro", "encro_e2e_tool")
  add_files("tests/e2e/*.cpp|fake_media_tool.cpp")
target_end()

includes("@builtin/xpack")

xpack("encro")
  set_formats("nsis", "srczip", "srctarxz", "zip", "tarxz")
  set_title("Encro")
  set_author("Artemiss")
  set_description("encro: Universal video encoder/converter/packer")
  add_targets("encro")
  add_sourcefiles("(src/**.h)", "(src/**.cpp)")
xpack_end()

