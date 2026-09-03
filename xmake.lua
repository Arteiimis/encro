add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })
set_version("0.1.5")

set_languages("c++26")

if is_plat("windows") then
  set_toolchains("clang-cl")
  set_toolset("ld", "lld-link")
  set_toolset("ar", "llvm-ar")
else
  set_toolchains("clang")
  -- clang + GNU ld 的 thin-LTO 链接不可靠（catch2/boost 符号被裁剪）；
  -- 统一 lld（与 Windows 的 lld-link 对齐，xmake 3 对包默认注入 lto）
  add_ldflags("-fuse-ld=lld", {force = true})
end

add_cxxflags("-ftrivial-auto-var-init=pattern")

add_plugindirs("./plugins/")

if is_mode("coverage") then
  -- 不用内置 mode.coverage 规则：它的 --coverage（gcov 插桩）会在 fork 后
  -- 逐文件 dump .gcda，拖慢/卡住 exec2 的子进程；这里只用 LLVM 插桩。
  set_policy("build.ccache", false)
  set_symbols("debug")
  set_optimize("none")
  add_cxxflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
  add_ldflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
end

if is_mode("releasedbg") then
  set_strip("none")
  add_runenvs("ASAN_OPTIONS", "poison_history_size=4096")
  set_policy("build.sanitizer.address", true)
end

if is_plat("windows") then
  add_defines("NOMINMAX")
  add_defines("WIN32_LEAN_AND_MEAN")
  add_defines("_MSVC_STL_HARDENING=1")
end

-- SPDLOG_ACTIVE_LEVEL per build mode (D-14)
-- release/releasedbg: strip TRACE and DEBUG at compile time
-- debug/coverage: keep all levels for development
if is_mode("release") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
elseif is_mode("releasedbg") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
elseif is_mode("debug") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
elseif is_mode("coverage") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
end

add_requires("boost[process,stacktrace,asio,context,date_time,json]")
add_requires("cli11")
add_requires("thread-pool")
add_requires("spdlog[fmt_external]")
add_requires("fmt")
add_requires("indicators")
add_requires("libzippp")
-- add_requireconfs("libzippp.libzip", {configs = {toolchains = "clang"}})
add_requires("catch2")

target("encro")
  set_kind("binary")

  if is_mode("release") then
    set_policy("build.optimization.lto", true)
  end

  add_packages("boost", "thread-pool", "indicators", "libzippp", "fmt", "spdlog", "cli11")
  if is_plat("windows") then
    add_syslinks("dbghelp", "shell32", "ole32")
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

-- Injects the absolute path of the fake media tool binary as a compile-time
-- define, so unit and e2e test binaries can spawn it directly.
local function injectFakeToolDefine(target)
  local dep = target:dep("encro_e2e_tool")
  if dep then
    local exe = path.absolute(dep:targetfile())
    target:add("defines", "FAKE_TOOL_EXE_PATH=\"" .. exe:gsub("\\", "\\\\") .. "\"")
  end
end

target("tests")
  set_kind("binary")
  set_default(false)

  add_packages("catch2", "boost", "thread-pool", "indicators", "fmt", "spdlog", "libzippp", "cli11")
  if is_plat("windows") then
    add_syslinks("dbghelp", "shell32", "ole32")
  else
    add_syslinks("dl")
  end
  add_includedirs("src", "tests")
  add_files("tests/*.cpp")
  add_files("tests/app/*.cpp")
  add_files("tests/infra/*.cpp")
  add_files("tests/picture/*.cpp")
  add_files("tests/preview/*.cpp")
  add_files("tests/video/*.cpp")
  add_files("src/**.cpp|main.cpp")

  -- Unit tests spawn the fake media tool exe directly (no cmd.exe layer).
  add_deps("encro_e2e_tool")
  after_load(injectFakeToolDefine)
target_end()

target("e2e_tests")
  set_kind("binary")
  set_default(false)

  add_packages("catch2", "boost", "libzippp", "fmt", "spdlog", "cli11")
  add_includedirs("tests")
  if is_plat("windows") then
    add_syslinks("dbghelp", "shell32", "ole32")
  else
    add_syslinks("dl")
  end

  add_deps("encro", "encro_e2e_tool")
  after_load(injectFakeToolDefine)
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

