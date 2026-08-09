task("include-cleaner")
  set_category("plugin")
  set_menu({
    usage = "xmake include-cleaner [options]",
    description = "Scan all TUs for unused includes via clang-include-cleaner (needs build/compile_commands.json)",
    options = {
      {"k", "check", "k", nil, "Exit non-zero if any unused include is found (CI mode)"},
      {"f", "filter", "v", nil, "Only report headers containing this substring (e.g. job_state)"},
      {"j", "jobs", "v", "16", "Parallel jobs, one TU per job (default: 16)"}
    }
  })

  on_run(function()
    local option = import("core.base.option")
    local tool = import("lib.detect.find_tool")("python3")
    if not tool then
      tool = import("lib.detect.find_tool")("python")
    end
    assert(tool, "python3/python not found on PATH")

    local script = path.join(os.projectdir(), "plugins", "include_cleaner", "scan.py")
    local argv = {script}
    if option.get("check") then
      table.insert(argv, "--check")
    end
    if option.get("filter") then
      table.insert(argv, "-f")
      table.insert(argv, option.get("filter"))
    end
    table.insert(argv, "-j")
    table.insert(argv, option.get("jobs") or "16")

    local ok = try {
      function()
        return os.execv(tool.program, argv, {curdir = os.projectdir()})
      end
    }
    if not ok then
      assert(false, option.get("check")
        and "include-cleaner check failed: unused include(s) found"
        or "include-cleaner scan failed")
    end
  end)
task_end()
