add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")
set_policy("build.optimization.lto", true)

set_languages("c++23")
set_toolchains("clang")

add_cxxflags("-Wno-c++26-extensions")
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
if is_plat("windows") then
  add_requires("libzippp[toolchains=clang-cl]")
else
  add_requires("libzippp")
end
add_requires("catch2")

target("encro")
  set_kind("binary")

  add_packages("boost", "thread-pool", "indicators", "spdlog", "libzippp")

  add_includedirs("src")
  add_files("src/**.cpp")
target_end()

target("tests")
  set_kind("binary")
  set_default(false)

  add_packages("catch2", "boost", "thread-pool", "indicators", "spdlog", "libzippp")
  add_includedirs("src")
  add_files("tests/*.cpp")
  add_files("src/**.cpp|main.cpp")
target_end()

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