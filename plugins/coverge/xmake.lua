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