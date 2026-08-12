-- test-report: run the unit tests with a JUnit report file and, on failure,
-- print a per-failure summary parsed from the report so the failing tests are
-- locatable from the first read of the output (agent-friendly, CI-parity).
task("test-report")
  set_category("plugin")
  set_menu({
    usage = "xmake test-report [options]",
    description = "Run unit tests, write build/last-test-report.xml, print failure summary",
    options = {
      {"", "tag", "v", nil, "Catch2 tag filter to run (e.g. [progress])"}
    }
  })

  on_run(function()
    local option = import("core.base.option")
    local config = import("core.project.config")

    local projectdir = os.projectdir()
    local platform = os.host()
    local arch = os.arch()
    local mode = config.get("mode") or "release"
    local ext = platform == "windows" and ".exe" or ""
    local tests_bin = path.join(projectdir, "build", platform, arch, mode, "tests" .. ext)
    local report_path = path.join(projectdir, "build", "last-test-report.xml")
    local console_path = path.join(projectdir, "build", "last-test-console.log")

    local process = import("core.base.process")

    -- os.execv returns nil for non-zero exits in xmake 3.1.0, so use the
    -- process API directly to get the real exit code.
    local function run(cmd, args, opts)
      local proc = process.openv(cmd, args, opts)
      if not proc then
        os.raise(string.format("failed to run %s", cmd))
      end
      local waitok, status = proc:wait(-1)
      proc:close()
      if not waitok or waitok <= 0 then
        os.raise(string.format("failed to run %s", cmd))
      end
      return status
    end

    -- Incremental build first (same execv pattern as the coverage plugin)
    run("xmake", {"build", "tests"})

    if not os.exists(tests_bin) then
      os.raise(string.format("tests binary not found: %s", tests_bin))
    end

    -- Two reporters work, but a global -o (binding the output file) makes
    -- this Catch2/clang-cl build fastfail; the inline "junit::out=" form
    -- binds the file to the junit reporter only and leaves console on stdout.
    --
    -- Child stdout goes to a file, not the terminal: TTY detection then
    -- sees a non-terminal, so progress-bar frames never render and the
    -- console output stays a clean text stream in every context (terminal,
    -- pipe, CI). The full console text survives in the log for inspection.
    -- stderr stays inherited: it carries low-frequency evidence (kept temp
    -- dir paths, crash records) that should surface directly in run output.
    local test_args = {"-r", "console", "-r", string.format("junit::out=%s", report_path)}
    local tag = option.get("tag")
    if tag then
      table.insert(test_args, tag)
    end
    local ec = run(tests_bin, test_args, {stdout = console_path})

    local function attr(name)
      return (io.readfile(report_path) or ""):match('<testsuite[^>]*' .. name .. '="(%d+)"')
    end
    local function print_failures(content)
      local failed = 0
      for testcase, body in content:gmatch('<testcase classname="[^"]*" name="([^"]*)"[^>]*>(.-)</testcase>') do
        local message = body:match('<failure[^>]*>(.-)</failure>')
        if message then
          failed = failed + 1
          cprint("${red}FAILED: %s", testcase)
          for line in message:gmatch("[^\n]+") do
            print("  " .. line)
          end
        end
      end
      return failed
    end

    if ec ~= 0 then
      local content = io.readfile(report_path) or ""
      local failed = print_failures(content)
      if failed == 0 then
        print("(no <failure> entries found in " .. report_path .. ")")
      end
      print("report: " .. report_path)
      print("console: " .. console_path)
      os.exit(ec)
    else
      -- Success summary: the report's <testcase> elements expand per section
      -- (not a test-case count), so take Catch2's own summary line from the
      -- console log instead; fall back to the report's assertion count.
      local summary, cases = (io.readfile(console_path) or ""):match(
        "All tests passed %((%d+) assertions in (%d+) test cases%)"
      )
      if summary then
        print(string.format("All tests passed (%s assertions in %s test cases)", summary, cases))
      else
        local assertions = attr("tests")
        if assertions then
          print(string.format("All tests passed (%s assertions)", assertions))
        else
          print("(empty report: " .. report_path .. ")")
        end
      end
    end
  end)
