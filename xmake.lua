add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")
set_policy("build.optimization.lto", true)
set_version("0.1.5")

set_languages("c++26")
set_toolchains("clang-cl")

add_cxxflags("-ftrivial-auto-var-init=pattern")

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
add_requires("spdlog[std_format]")
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

  add_packages("boost", "thread-pool", "indicators", "spdlog", "libzippp", "immer")
  if is_plat("windows") then
    add_syslinks("dbghelp")
  else
    add_syslinks("dl")
  end

  add_includedirs("src")
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

  add_packages("catch2", "boost", "thread-pool", "indicators", "spdlog", "libzippp", "immer")
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

  add_packages("catch2", "boost", "libzippp", "immer")
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

task("coverage")
  set_category("plugin")
  set_menu({
    usage = "xmake coverage [options]",
    description = "Run tests under coverage and print llvm-cov report",
    options = {
      {"", "summary", "k", nil, "Show summary-only report"}
    }
  })

  on_run(function()
    local option = import("core.base.option")

    local coverage_dir = path.join(os.projectdir(), "build", "coverage")
    os.mkdir(coverage_dir)

    os.execv("xmake", {"f", "-m", "coverage", "-c"})
    os.execv("xmake", {"build", "tests"})

    local profile_tpl = path.join(coverage_dir, "tests-%p.profraw")
    os.execv("xmake", {"run", "tests"}, {envs = {LLVM_PROFILE_FILE = profile_tpl}})

    local prof_files = os.files(path.join(coverage_dir, "tests-*.profraw"))
    assert(#prof_files > 0, "No .profraw files generated")

    local merged = path.join(coverage_dir, "tests.profdata")
    local merge_args = {"merge", "-sparse"}
    for _, f in ipairs(prof_files) do table.insert(merge_args, f) end
    table.insert(merge_args, "-o")
    table.insert(merge_args, merged)
    os.execv("llvm-profdata", merge_args)

    local tests_bin = path.join(os.projectdir(), "build", os.host(), os.arch(), "coverage", "tests.exe")
    if not os.exists(tests_bin) then
      tests_bin = path.join(os.projectdir(), "build", "windows", "x64", "coverage", "tests.exe")
    end

    local report_args = {"report", tests_bin, "-instr-profile", merged}
    if option.get("summary") then table.insert(report_args, "--summary-only") end
    os.execv("xmake", {"f", "-m", "release", "-c"})
    os.execv("llvm-cov", report_args)
  end)
task_end()

task("format")
  set_category("plugin")
  set_menu({
    usage = "xmake format [options]",
    description = "Format C/C++ sources with clang-format",
    options = {
      {"", "check", "k", nil, "Check formatting without modifying files"}
    }
  })

  on_run(function()
    local option = import("core.base.option")

    local style_file = "D:/clangformat/.clang-format"
    assert(os.isfile(style_file), "clang-format config not found: " .. style_file)

    local patterns = {}
    local roots = {"src", "tests"}
    local exts = {"c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "ipp", "inl"}
    for _, root in ipairs(roots) do
      for _, ext in ipairs(exts) do
        table.insert(patterns, path.join(root, "**." .. ext))
      end
    end

    local files = {}
    local seen = {}
    for _, pattern in ipairs(patterns) do
      for _, file in ipairs(os.files(pattern)) do
        if not seen[file] then
          seen[file] = true
          table.insert(files, file)
        end
      end
    end

    table.sort(files)
    assert(#files > 0, "No C/C++ files found to format")

    local args = {string.format("-style=file:%s", style_file)}
    if option.get("check") then
      table.insert(args, "--dry-run")
      table.insert(args, "--Werror")
    else
      table.insert(args, "-i")
    end

    for _, file in ipairs(files) do
      local file_args = table.clone(args)
      table.insert(file_args, file)
      os.execv("clang-format", file_args)
    end

    if option.get("check") then
      print(string.format("clang-format check passed for %d files", #files))
    else
      print(string.format("clang-format applied to %d files", #files))
    end
  end)
task_end()