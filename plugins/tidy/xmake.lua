task("tidy")
  set_category("plugin")
  set_menu({
    usage = "xmake tidy [options]",
    description = "Static analysis via clang-tidy over all TUs (needs build/compile_commands.json)",
    options = {
      {"s", "sarif", "k", nil, "Write build/tidy-results.sarif instead of text"},
      {"a", "analyzer", "k", nil, "Include the slow clang static analyzer checks (deep scan)"},
      {"f", "filter", "kv", nil, "Only scan TUs whose path contains this substring (e.g. video)"},
      {"c", "checks", "kv", nil, "Override clang-tidy checks (e.g. '-*,bugprone-*'); cached per checks set"},
      {"j", "jobs", "kv", nil, "Parallel jobs (default: 8, 4 with --analyzer)"},
      {nil, "selftest", "k", nil, "Run the bundled check-set/header-filter self-test"}
    }
  })

  on_run(function()
    local option = import("core.base.option")
    local tool = import("lib.detect.find_tool")("python3")
    if not tool then
      tool = import("lib.detect.find_tool")("python")
    end
    assert(tool, "python3/python not found on PATH")

    local script = path.join(os.projectdir(), "plugins", "tidy", "scan.py")
    local argv = {script}
    if option.get("sarif") then
      table.insert(argv, "--sarif")
    end
    if option.get("analyzer") then
      table.insert(argv, "--analyzer")
    end
    if option.get("filter") then
      table.insert(argv, "-f")
      table.insert(argv, option.get("filter"))
    end
    if option.get("checks") then
      table.insert(argv, "--checks")
      table.insert(argv, option.get("checks"))
    end
    if option.get("selftest") then
      table.insert(argv, "--selftest")
    end
    if option.get("jobs") then
      table.insert(argv, "-j")
      table.insert(argv, option.get("jobs"))
    end

    local ret = os.execv(tool.program, argv, {curdir = os.projectdir(), try = true})
    if ret ~= 0 then
      os.raise(string.format("tidy failed (exit %s); see output above", tostring(ret)))
    end
  end)
task_end()
