task("coverage")
  set_category("plugin")
  set_menu({
    usage = "xmake coverage [options]",
    description = "Run tests (and optionally e2e) under coverage and print llvm-cov report",
    options = {
      {"", "summary", "k", nil, "Show summary-only report"},
      {"", "e2e", "k", nil, "Also build and run e2e tests under coverage"},
      {"", "keep", "k", nil, "Keep coverage build mode after finishing"}
    }
  })

  on_run(function()
    local option = import("core.base.option")

    local projectdir = os.projectdir()
    local platform = os.host()
    local coverage_dir = path.join(projectdir, "build", "coverage")
    local mode_dir = path.join(projectdir, "build", platform, os.arch(), "coverage")
    local ext = platform == "windows" and ".exe" or ""

    local function run(cmd, args, opts)
      local ret = try {
        function()
          return os.execv(cmd, args, opts)
        end
      }
      if not ret then
        os.raise(string.format("failed to run %s", cmd))
      end
      if ret ~= 0 then
        os.raise(string.format(
          "command failed (%s): %s %s",
          tostring(ret),
          cmd,
          table.concat(args, " ")
        ))
      end
    end

    local function find_tool(name)
      local tool = import("lib.detect.find_tool")(name)
      if not tool then
        os.raise(string.format("%s not found on PATH; install LLVM or add it to PATH", name))
      end
      return tool.program
    end

    -- 插桩自检：coverage 模式下构建后 compile_commands.json 必须含插桩标志，
    -- 否则 xmake 的 flag auto-check 静默丢弃了它们（add_cxxflags 缺 {force = true}），
    -- 整个流程会"成功"但产不出任何 profraw。
    local function assert_instrumented()
      local cc_file = path.join(projectdir, "build", "compile_commands.json")
      if not os.exists(cc_file) then
        os.raise("build/compile_commands.json missing; keep plugin.compile_commands.autoupdate enabled")
      end
      local json = import("core.base.json")
      local entries = json.decode(io.readfile(cc_file))
      for _, e in ipairs(entries) do
        local cmdline = e.command or table.concat(e.arguments or {}, " ")
        if cmdline:find("%-fprofile%-instr%-generate") then
          return
        end
      end
      os.raise(
        "coverage instrumentation missing from compile_commands.json; "
          .. "add {force = true} to the coverage cxxflags/ldflags in xmake.lua"
      )
    end

    os.mkdir(coverage_dir)
    -- 清理旧 profraw，避免把上次运行的数据混进本次合并
    os.rm(path.join(coverage_dir, "*.profraw"))

    local failed = nil
    try {
      function()
        run("xmake", {"f", "-m", "coverage", "-c", "-p", platform, "-y"})
        run("xmake", {"build", "tests"})
        assert_instrumented()

        -- 直接运行二进制：xmake 3.1.0 的 run 在 Linux 上 execv 失败
        local tests_bin = path.join(mode_dir, "tests" .. ext)
        run(tests_bin, {}, {
          envs = {LLVM_PROFILE_FILE = path.join(coverage_dir, "tests-%p.profraw")}
        })

        if option.get("e2e") then
          run("xmake", {"build", "e2e_tests", "encro", "encro_e2e_tool"})
          local e2e_bin = path.join(mode_dir, "e2e_tests" .. ext)
          run(e2e_bin, {}, {
            envs = {LLVM_PROFILE_FILE = path.join(coverage_dir, "e2e-%p.profraw")}
          })
        end

        local prof_files = os.files(path.join(coverage_dir, "*.profraw"))
        if #prof_files == 0 then
          os.raise("no .profraw files generated; instrumentation did not run")
        end

        local merged = path.join(coverage_dir, "all.profdata")
        local merge_args = {"merge", "-sparse"}
        for _, f in ipairs(prof_files) do
          table.insert(merge_args, f)
        end
        table.insert(merge_args, "-o")
        table.insert(merge_args, merged)
        run(find_tool("llvm-profdata"), merge_args)

        local report_args = {"report"}
        for _, name in ipairs({"tests", "e2e_tests", "encro", "encro_e2e_tool"}) do
          local bin = path.join(mode_dir, name .. ext)
          if os.exists(bin) then
            table.insert(report_args, bin)
          end
        end
        table.insert(report_args, "-instr-profile")
        table.insert(report_args, merged)
        -- 只统计项目代码，过滤第三方头文件（boost/catch2/spdlog/fmt/immer/indicators/libzip/thread-pool/asio）
        table.insert(report_args, "-ignore-filename-regex=(^|[\\\\/])(boost|catch2|spdlog|fmt|immer|indicators|libzip|thread%-pool|asio)([\\\\/]|$)")
        if option.get("summary") then
          table.insert(report_args, "--summary-only")
        end
        run(find_tool("llvm-cov"), report_args)
      end,
      catch {
        function(e)
          failed = e
        end
      }
    }

    -- 无论成败都恢复 release 模式（--keep 时跳过，便于连续多次跑覆盖率）
    if not option.get("keep") then
      run("xmake", {"f", "-m", "release", "-c", "-p", platform, "-y"})
    end

    if failed then
      cprint("${red}coverage failed: %s", failed)
      os.exit(1)
    end
  end)
task_end()
