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
      local envs = {"TMP=" .. shard_dir, "TEMP=" .. shard_dir}
      for k, v in pairs(os.getenvs()) do
        local key = k:upper()
        if key ~= "TMP" and key ~= "TEMP" then envs[#envs + 1] = k .. "=" .. v end
      end
      return envs
    end

    -- Spawn one process per shard without waiting: they all run concurrently,
    -- then a single wait pass collects every exit code.
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
        local proc = process.openv(
          binary,
          {
            "--shard-count", tostring(count),
            "--shard-index", tostring(i),
            "--durations", "yes"
          },
          {envs = shard_envs(tmp), stdout = path.join(workdir, string.format("shard-%d.log", i))}
        )
        if not proc then
          os.raise(string.format("failed to spawn %s shard %d", binary, i))
        end
        -- 1-based: the wait pass below iterates with ipairs and would skip a
        -- 0-keyed entry, leaking the process and losing its result.
        procs[#procs + 1] = proc
      end
      return procs
    end

    local function shard_status(proc, logfile)
      -- wait()'s status is unreliable when many processes run concurrently
      -- (poller event bookkeeping under parallel waits); the Catch2 console
      -- log is authoritative, so the return value is discarded.
      proc:wait(-1)
      proc:close()
      local content = io.readfile(logfile) or ""
      -- Success always ends with a completion marker; failures carry
      -- FAILED/failed lines, and a crash mid-run leaves no marker at all.
      local function passed()
        if content:find("FAILED", 1, true) then return false end
        local nfailed = content:match("| (%d+) failed")
        if nfailed and tonumber(nfailed) > 0 then return false end
        return content:find("All tests passed", 1, true) ~= nil
          or content:find("test cases: ", 1, true) ~= nil
          or content:find("assertions: ", 1, true) ~= nil
      end
      local assertions, cases = content:match(
        "All tests passed %((%d+) assertions in (%d+) test cases%)"
      )
      if not assertions then
        -- Skipped real-ffmpeg tests switch Catch2 to the "test cases: N | X
        -- passed" summary; count the passed columns so skipped cases are not
        -- reported as run. Columns are space-padded, so %s+ everywhere.
        assertions = content:match("assertions:%s+%d+%s*|%s+(%d+)%s+passed")
        cases = content:match("test cases:%s+%d+%s*|%s+(%d+)%s+passed")
      end
      return passed(), assertions and tonumber(assertions), cases and tonumber(cases)
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
    run("xmake", {"build", "tests", "e2e_tests"})

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
    for i, proc in ipairs(unit_procs) do
      procs[#procs + 1] = {
        name = string.format("unit shard %d", i - 1),
        proc = proc,
        logfile = path.join(workdir, "unit", string.format("shard-%d.log", i - 1))
      }
    end
    local e2e_procs = spawn_shards(e2e_bin, e2e_shards, path.join(workdir, "e2e"))
    for i, proc in ipairs(e2e_procs) do
      procs[#procs + 1] = {
        name = string.format("e2e shard %d", i - 1),
        proc = proc,
        logfile = path.join(workdir, "e2e", string.format("shard-%d.log", i - 1))
      }
    end

    local passed_assertions = 0
    local passed_cases = 0
    local failures = {}
    for _, p in ipairs(procs) do
      local passed, assertions, cases = shard_status(p.proc, p.logfile)
      if passed and assertions then
        passed_assertions = passed_assertions + assertions
        passed_cases = passed_cases + (cases or 0)
      else
        failures[#failures + 1] = p
      end
    end

    if #failures == 0 then
      cprint(
        "${bright green}All tests passed (%d assertions in %d test cases) across %d parallel shards",
        passed_assertions,
        passed_cases,
        #procs
      )
      return
    end

    for _, p in ipairs(failures) do
      cprint("${red}FAILED: %s — log: %s", p.name, p.logfile)
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
