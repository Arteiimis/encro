-- test-parallel: run the unit and e2e suites in parallel Catch2 shards,
-- each with its own isolated temp root so shared scratch/log/TempDir state
-- (all rooted at fs::temp_directory_path()) never collides across shards.
--
-- Speedup on 16 cores: unit 49.5s -> ~9.4s (8 shards), e2e 38.4s -> ~12.3s
-- (4 shards); both suites in parallel land around 13s wall clock.
task("test-parallel")
  set_category("plugin")
  set_menu({
    usage = "xmake test-parallel [options]",
    description = "Run unit + e2e tests in parallel shards with isolated temp dirs",
    options = {
      {nil, "unit-shards", "kv", nil, "Shard count for unit tests (default: cores/2, capped at 8)"},
      {nil, "e2e-shards", "kv", nil, "Shard count for e2e tests (default: cores/4, capped at 4)"}
    }
  })

  on_run(function()
    local option = import("core.base.option")
    local config = import("core.project.config")
    local process = import("core.base.process")

    local platform = config.plat() or os.host()
    local arch = os.arch()
    local mode = config.get("mode") or "release"
    local builddir = config.builddir({absolute = true})
    local ext = platform == "windows" and ".exe" or ""
    local bin_dir = path.join(builddir, platform, arch, mode)

    -- os.execv raises on non-zero exits and hides the exit code, so use the
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

    local function makedirs(dir)
      if os.isdir(dir) then return end
      local parent = path.directory(dir)
      if parent and parent ~= dir then makedirs(parent) end
      os.mkdir(dir)
    end

    -- The C layer REPLACES the child environment with the envs list, so the
    -- whole current environment must be carried over (powershell/.NET fail
    -- without USERPROFILE, SystemRoot, ...). TMP/TEMP are overridden per
    -- shard to isolate the shared temp roots.
    local function shard_envs(shard_dir)
      -- TMP/TEMP for Windows and TMPDIR for POSIX: every consumer of the
      -- temporary root must see the shard's own directory.
      local envs = {"TMP=" .. shard_dir, "TEMP=" .. shard_dir, "TMPDIR=" .. shard_dir}
      for k, v in pairs(os.getenvs()) do
        local key = k:upper()
        if key ~= "TMP" and key ~= "TEMP" and key ~= "TMPDIR" then
          envs[#envs + 1] = k .. "=" .. v
        end
      end
      return envs
    end

    -- Spawn one process per shard without waiting: they all run concurrently,
    -- then a single wait pass collects every result. Each shard writes its own
    -- console log (evidence) and its own JUnit report (the verdict).
    local function spawn_shards(binary, count, workdir)
      local procs = {}
      for i = 0, count - 1 do
        local shard_dir = path.join(workdir, string.format("%d", i))
        makedirs(shard_dir)
        -- Forward slashes keep the values intact; see shard_envs for the
        -- full-environment contract. --durations records per-test seconds in
        -- the shard log so a loaded run's slowest cases are identifiable
        -- post-mortem; duration lines contain no verdict substrings.
        local tmp = shard_dir:gsub("\\", "/")
        local logfile = path.join(workdir, string.format("shard-%d.log", i))
        local report = path.join(workdir, string.format("shard-%d.xml", i)):gsub("\\", "/")
        local proc = process.openv(
          binary,
          {
            "--shard-count", tostring(count),
            "--shard-index", tostring(i),
            "--durations", "yes",
            "-r", "console",
            "-r", "junit::out=" .. report
          },
          {envs = shard_envs(tmp), stdout = logfile}
        )
        if not proc then
          os.raise(string.format("failed to spawn %s shard %d", binary, i))
        end
        -- 1-based: the wait pass below iterates with ipairs and would skip a
        -- 0-keyed entry, leaking the process and losing its result.
        procs[#procs + 1] = {
          proc = proc,
          logfile = logfile,
          report = report,
          tmp = tmp
        }
      end
      return procs
    end

    -- The shard's own JUnit report decides its verdict: log text is evidence
    -- only, and wait() statuses are unreliable when many processes run
    -- concurrently (poller event bookkeeping under parallel waits). A shard
    -- that dies mid-run leaves no complete report, which counts as failed.
    local function shard_verdict(p)
      local content = io.readfile(p.report)
      if not content then
        return false, "no report written (shard died mid-run)"
      end
      local failures = content:match('failures="(%d+)"')
      local errors = content:match('errors="(%d+)"')
      if not failures or not errors then
        return false, "report unreadable"
      end
      if tonumber(failures) > 0 or tonumber(errors) > 0 then
        return false, string.format("%s failure(s), %s error(s)", failures, errors)
      end
      return true, nil
    end

    -- Summary numbers come from the shard's console log: the JUnit tests=
    -- attribute counts sections, not test cases. The child writes that summary
    -- through a redirected stdout, so a read landing before the file is whole
    -- would under-count silently; retry briefly, and report no counts at all
    -- rather than a short total (the caller then omits the aggregate).
    local function read_shard_log(logfile)
      local content = nil
      for _ = 1, 40 do
        content = io.readfile(logfile)
        if content and content:find("assertions", 1, true) then return content end
        os.sleep(50)
      end
      return content
    end

    local function shard_counts(logfile)
      local content = read_shard_log(logfile) or ""
      local assertions, cases = content:match(
        "All tests passed %((%d+) assertions in (%d+) test cases%)"
      )
      if not assertions then
        -- Table form (a skipped or failed case switches Catch2 to it). Take
        -- the total columns, not the passed ones: both summary forms have to
        -- produce the same aggregate, and the total is what a single-process
        -- run reports.
        assertions = content:match("assertions:%s+(%d+)%s*|")
        cases = content:match("test cases:%s+(%d+)%s*|")
      end
      return assertions and tonumber(assertions) or 0, cases and tonumber(cases) or 0
    end

    local function cpu_count()
      local n = tonumber(os.getenv("NUMBER_OF_PROCESSORS") or "")
      if n then return n end
      local info = os.cpuinfo()
      return info and info.ncpu or 4
    end

    local function clamp_shards(cores, divisor, max)
      return math.min(max, math.max(1, math.floor(cores / divisor)))
    end

    local cores = cpu_count()
    local unit_shards = tonumber(option.get("unit-shards")) or clamp_shards(cores, 2, 8)
    local e2e_shards = tonumber(option.get("e2e-shards")) or clamp_shards(cores, 4, 4)
    if unit_shards < 1 or e2e_shards < 1 then
      os.raise("shard counts must be >= 1")
    end

    -- Incremental build first (same execv pattern as the coverage plugin).
    -- run() returns the exit code instead of raising, so check it here: a
    -- failed build would otherwise shard the stale binaries.
    if run("xmake", {"build", "tests", "e2e_tests"}) ~= 0 then
      os.raise("tests build failed; refusing to shard stale binaries")
    end

    local tests_bin = path.join(bin_dir, "tests" .. ext)
    local e2e_bin = path.join(bin_dir, "e2e_tests" .. ext)
    for _, bin in ipairs({tests_bin, e2e_bin}) do
      if not os.exists(bin) then
        os.raise(string.format("test binary not found: %s", bin))
      end
    end

    local workdir = path.join(builddir, ".test-parallel")
    if os.isdir(workdir) then os.rmdir(workdir) end
    makedirs(workdir)

    -- All shards spawn up front; processes run as they are created, so the
    -- wait pass below only harvests results.
    local procs = {}
    local unit_procs = spawn_shards(tests_bin, unit_shards, path.join(workdir, "unit"))
    for i, p in ipairs(unit_procs) do
      p.name = string.format("unit shard %d", i - 1)
      procs[#procs + 1] = p
    end
    local e2e_procs = spawn_shards(e2e_bin, e2e_shards, path.join(workdir, "e2e"))
    for i, p in ipairs(e2e_procs) do
      p.name = string.format("e2e shard %d", i - 1)
      procs[#procs + 1] = p
    end

    local passed_assertions = 0
    local passed_cases = 0
    local counts_complete = true
    local failures = {}
    for _, p in ipairs(procs) do
      -- Reap the process (its status is not trustworthy here), then judge the
      -- shard by its own report.
      p.proc:wait(-1)
      p.proc:close()
      local passed, reason = shard_verdict(p)
      if passed then
        local assertions, cases = shard_counts(p.logfile)
        if assertions == 0 and cases == 0 then counts_complete = false end
        passed_assertions = passed_assertions + assertions
        passed_cases = passed_cases + cases
      else
        p.reason = reason
        failures[#failures + 1] = p
      end
    end

    if #failures == 0 then
      if counts_complete then
        cprint(
          "${bright green}All tests passed (%d assertions in %d test cases) across %d parallel shards",
          passed_assertions,
          passed_cases,
          #procs
        )
      else
        -- Never print a total that a missing shard summary made short.
        cprint(
          "${bright green}All tests passed across %d parallel shards (per-shard counts in the shard logs)",
          #procs
        )
      end
      cprint("${dim}per-shard temp roots under %s (TMP/TEMP/TMPDIR)", workdir)
      return
    end

    for _, p in ipairs(failures) do
      cprint("${red}FAILED: %s — %s — log: %s", p.name, p.reason, p.logfile)
      local content = io.readfile(p.logfile) or ""
      local shown = 0
      for line in content:gmatch("[^\n]+") do
        if line:find("FAILED", 1, true) then
          print("  " .. line)
          shown = shown + 1
          if shown >= 8 then break end
        end
      end
      if shown == 0 then
        for line in content:gmatch("[^\n]+") do
          if line:find("assertions", 1, true) then
            print("  " .. line)
          end
        end
      end
    end
    os.exit(1)
  end)
