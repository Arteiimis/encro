-- test-parallel: run the unit and e2e suites in parallel shards, partitioned by
-- enumerated case name (spec files, cost-packed) instead of by the runner's
-- randomised order, each shard with its own temp root so shared scratch/log state
-- never collides. A shard that executes fewer cases than it was assigned, or that
-- prints no countable summary, fails; each suite prints the sum of its shards' own
-- totals, so the unit number equals a single-process run's. See
-- openspec/specs/deterministic-test-sync/spec.md.
task("test-parallel")
  set_category("plugin")
  set_menu({
    usage = "xmake test-parallel [options]",
    description = "Run unit + e2e tests in parallel, partitioned by case name",
    options = {
      {nil, "unit-shards", "kv", nil, "Shard count for unit tests (default: cores/2, capped at 8)"},
      {nil, "e2e-shards", "kv", nil, "Shard count for e2e tests (default: cores/4, capped at 4)"},
      {nil, "selftest", "k", nil, "Check the partitioning helpers on fixtures and exit"}
    }
  })

  on_run(function()
    local option = import("core.base.option")
    local config = import("core.project.config")
    local process = import("core.base.process")

    -- xmake's io.readfile raises on a missing file, so every read goes through
    -- this: a shard that died mid-run leaves no log or report, and that is a
    -- verdict input, not an error.
    local function readcontent(file)
      if not os.exists(file) then
        return nil
      end
      return io.readfile(file)
    end

    ----------------------------------------------------------------- helpers
    -- Kept free of process spawning and of raising, so --selftest can drive
    -- them on fixtures; callers own the actual failure reporting.

    -- '<seconds> s: <name>' rows from Catch2's --durations yes table. Section
    -- rows look identical (and may be indented), so the name is trimmed and
    -- callers keep only names they enumerated.
    local function parse_durations(content)
      local costs = {}
      for line in content:gmatch("[^\n]+") do
        local seconds, name = line:match("^%s*(%d+%.?%d*) s:%s*(.-)%s*$")
        if seconds and name ~= "" and not costs[name] then
          costs[name] = tonumber(seconds)
        end
      end
      return costs
    end

    -- Catch2 prints "All tests passed (N assertions in M test cases)" when
    -- nothing is skipped or failed - singular when M is 1 - and a totals table
    -- otherwise. Both carry the numbers this task reports, and a shard that
    -- prints neither is unaccounted for.
    local function parse_summary(content)
      local assertions, cases =
        content:match("All tests passed %((%d+) assertions in (%d+) test cases?%)")
      if not assertions then
        assertions = content:match("assertions:%s+(%d+)%s*|")
        cases = content:match("test cases?:%s+(%d+)%s*|")
      end
      if not assertions or not cases then
        return nil
      end
      return tonumber(assertions), tonumber(cases)
    end

    -- Case names from the machine-readable listing: one <Name> per <TestCase>.
    -- The console listing wraps long names, so it cannot be parsed for names.
    local function xml_case_names(xml)
      local names = {}
      for name in xml:gmatch("<Name>(.-)</Name>") do
        names[#names + 1] = name:gsub("&lt;", "<"):gsub("&gt;", ">"):gsub("&quot;", '"')
          :gsub("&apos;", "'"):gsub("&amp;", "&")
      end
      return names
    end

    -- The count the human-readable listing declares ("723 test cases"), used
    -- as a cross-check that the XML parse saw the whole listing.
    local function console_test_count(text)
      local count
      for n in text:gmatch("(%d+) test cases?%s*\n") do
        count = tonumber(n) -- the trailing summary wins
      end
      return count
    end

    -- Catch2 test specs treat these characters specially: it consumes the
    -- backslash as an escape, the comma as an OR. The suite uses only the comma
    -- today, so the comma is escaped and the rest are rejected by name rather
    -- than silently allowed to over-match or to select the wrong case.
    local kReserved = {"?", "*", "[", "]", "~", '"', "\\"}

    local function escape_spec(name)
      return (name:gsub(",", "\\,"))
    end

    local function reserved_in(name)
      for _, ch in ipairs(kReserved) do
        if name:find(ch, 1, true) then
          return ch
        end
      end
      return nil
    end

    -- Why an enumeration cannot be trusted, or nil when it is sound.
    local function enumeration_error(names, declared)
      if #names == 0 then
        return "the listing contained no test cases"
      end
      if not declared then
        return "the console listing declared no case count, so the two listings cannot be cross-checked"
      end
      if declared ~= #names then
        return string.format("the listings disagree: %d case(s) in the XML listing, %d declared by the console listing", #names, declared)
      end
      local seen = {}
      for _, name in ipairs(names) do
        local ch = reserved_in(name)
        if ch then
          return string.format("case name %q contains the reserved character %q, which the escaping does not cover", name, ch)
        end
        if seen[name] then
          return string.format("case name %q appears more than once, so a spec line cannot select exactly one case", name)
        end
        seen[name] = true
      end
      return nil
    end

    local function median(numbers)
      if #numbers == 0 then
        return nil
      end
      table.sort(numbers)
      local mid = math.floor((#numbers + 1) / 2)
      if #numbers % 2 == 1 then
        return numbers[mid]
      end
      return (numbers[mid] + numbers[mid + 1]) / 2
    end

    -- Longest-processing-time-first: heaviest case into the currently lightest
    -- shard, with ties broken by name so the same enumeration and cost model
    -- always produce the same assignment. A case with no recorded cost, or one
    -- the durations report as sub-millisecond (0.000 s, which Catch2's rounding
    -- produces for most trivial cases), is priced at the median of the positive
    -- costs instead of at zero: a free case would let a shard look empty while
    -- it still pays that case's setup time, and the near-free cases would pile
    -- onto one shard.
    local function partition(names, costs, shard_count)
      local known = {}
      for _, name in ipairs(names) do
        local cost = costs[name]
        if cost and cost > 0 then
          known[#known + 1] = cost
        end
      end
      local fallback = median(known) or 1
      local items = {}
      for _, name in ipairs(names) do
        local cost = costs[name]
        items[#items + 1] = {name = name, cost = (cost and cost > 0) and cost or fallback}
      end
      table.sort(items, function(a, b)
        if a.cost == b.cost then
          return a.name < b.name
        end
        return a.cost > b.cost
      end)
      local shards = {}
      for i = 1, shard_count do
        shards[i] = {names = {}, cost = 0}
      end
      for _, item in ipairs(items) do
        local lightest = 1
        for i = 2, shard_count do
          if shards[i].cost < shards[lightest].cost then
            lightest = i
          end
        end
        local shard = shards[lightest]
        shard.names[#shard.names + 1] = item.name
        shard.cost = shard.cost + item.cost
      end
      return shards, fallback
    end

    -- Why an assignment cannot be trusted, or nil when it is sound. The runner
    -- skips a valid spec line that matches nothing, so a short or overlapping
    -- assignment would silently run less (or more) than the suite holds.
    local function assignment_error(names, shards)
      if #shards > #names then
        return string.format("shard count %d exceeds the %d enumerated case(s); lower it so every shard gets a case", #shards, #names)
      end
      for i, shard in ipairs(shards) do
        if #shard.names == 0 then
          -- An empty spec file is 'no filter' to the runner, which would run
          -- the whole suite in that shard.
          return string.format("shard %d has no assigned case(s)", i - 1)
        end
      end
      local seen = {}
      local lines = 0
      local duplicates = {}
      for _, shard in ipairs(shards) do
        for _, name in ipairs(shard.names) do
          lines = lines + 1
          if seen[name] then
            duplicates[#duplicates + 1] = name
          end
          seen[name] = true
        end
      end
      local missing = {}
      for _, name in ipairs(names) do
        if not seen[name] then
          missing[#missing + 1] = name
        end
      end
      if lines ~= #names or #missing > 0 or #duplicates > 0 then
        return string.format(
          "the assignment covers %d of %d case(s)%s%s",
          lines,
          #names,
          #missing > 0 and string.format("; missing %q", missing[1]) or "",
          #duplicates > 0 and string.format("; duplicated %q", duplicates[1]) or ""
        )
      end
      return nil
    end

    local function cost_model_path(builddir, suite)
      return path.join(builddir, string.format(".test-cost-%s.txt", suite))
    end

    -- One '<name>\t<seconds>' per case: the model lives outside the wiped work
    -- directory so a later run can pack by measured cost.
    local function load_costs(file)
      local content = readcontent(file)
      if not content then
        return {}
      end
      local costs = {}
      for line in content:gmatch("[^\n]+") do
        local name, seconds = line:match("^(.-)\t(%d+%.?%d*)$")
        if name and name ~= "" then
          costs[name] = tonumber(seconds)
        end
      end
      return costs
    end

    local function save_costs(file, costs)
      local lines = {}
      for name, seconds in pairs(costs) do
        lines[#lines + 1] = string.format("%s\t%.3f", name, seconds)
      end
      table.sort(lines)
      io.writefile(file, table.concat(lines, "\n") .. "\n")
    end

    --------------------------------------------------------------- process side

    local platform = config.plat() or os.host()
    local arch = os.arch()
    local mode = config.get("mode") or "release"
    local builddir = config.builddir({absolute = true})
    local ext = platform == "windows" and ".exe" or ""
    local bin_dir = path.join(builddir, platform, arch, mode)

    local function makedirs(dir)
      if os.isdir(dir) then
        return
      end
      local parent = path.directory(dir)
      if parent and parent ~= dir then
        makedirs(parent)
      end
      os.mkdir(dir)
    end

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

    -- Enumerate a suite and check the enumeration before anything runs.
    local function enumerate(binary, workdir, suite)
      local xml_file = path.join(workdir, string.format("%s-listing.xml", suite))
      local txt_file = path.join(workdir, string.format("%s-listing.txt", suite))
      run(binary, {"--list-tests", "-r", "xml"}, {stdout = xml_file})
      run(binary, {"--list-tests", "-r", "console"}, {stdout = txt_file})
      local names = xml_case_names(readcontent(xml_file) or "")
      local declared = console_test_count(readcontent(txt_file) or "")
      local err = enumeration_error(names, declared)
      if err then
        os.raise(string.format("%s enumeration is unsound: %s", suite, err))
      end
      print(string.format("%s: %d test cases enumerated", suite, #names))
      return names
    end

    -- The escaping is the one transformation the runner can silently reject,
    -- so round-trip the first comma-containing name through a real spec file.
    local function preflight_escaping(binary, names, workdir, suite)
      local probe
      for _, name in ipairs(names) do
        if name:find(",", 1, true) then
          probe = name
          break
        end
      end
      if not probe then
        print(string.format("%s: no case name needs escaping", suite))
        return
      end
      local spec = path.join(workdir, string.format("%s-escape-probe.txt", suite))
      local listing = path.join(workdir, string.format("%s-escape-probe.log", suite))
      io.writefile(spec, escape_spec(probe) .. "\n")
      run(binary, {"--list-tests", "-f", spec, "-r", "console"}, {stdout = listing})
      local matched = (readcontent(listing) or ""):match("(%d+) matching test cases?")
      if tonumber(matched) ~= 1 then
        os.raise(string.format(
          "%s: the escaped name %q selected %s case(s) instead of 1",
          suite,
          probe,
          matched or "no"
        ))
      end
      print(string.format("%s: escaped name %q selects 1 case", suite, probe))
    end

    -- One spec file per shard: the runner reads one spec per line and skips a
    -- valid line that matches nothing, which is what the count checks catch.
    local function write_specs(names, suite_dir, suite, shard_count, costs)
      local shards = partition(names, costs, shard_count)
      local err = assignment_error(names, shards)
      if err then
        os.raise(string.format("%s: %s", suite, err))
      end
      for i, shard in ipairs(shards) do
        shard.spec = path.join(suite_dir, string.format("shard-%d.specs.txt", i - 1))
        local lines = {}
        for _, name in ipairs(shard.names) do
          lines[#lines + 1] = escape_spec(name)
        end
        io.writefile(shard.spec, table.concat(lines, "\n") .. "\n")
      end
      return shards
    end

    -- Spawn one process per shard without waiting: they all run concurrently,
    -- then a single wait pass collects every result. Each shard writes its own
    -- console log (evidence and counts) and its own JUnit report (the verdict).
    local function spawn_shards(binary, shards, workdir, suite)
      local procs = {}
      for i, shard in ipairs(shards) do
        local shard_dir = path.join(workdir, string.format("%d", i - 1))
        makedirs(shard_dir)
        -- Forward slashes keep the values intact; see shard_envs for the
        -- full-environment contract. --durations records per-test seconds in
        -- the shard log so a loaded run's slowest cases are identifiable
        -- post-mortem, and feeds the next run's cost model.
        local tmp = shard_dir:gsub("\\", "/")
        local logfile = path.join(workdir, string.format("shard-%d.log", i - 1))
        local report = path.join(workdir, string.format("shard-%d.xml", i - 1)):gsub("\\", "/")
        local proc = process.openv(
          binary,
          {
            "-f", shard.spec,
            "--durations", "yes",
            "-r", "console",
            "-r", "junit::out=" .. report
          },
          {envs = shard_envs(tmp), stdout = logfile}
        )
        if not proc then
          os.raise(string.format("failed to spawn %s shard %d", binary, i - 1))
        end
        -- 1-based: the wait pass below iterates with ipairs and would skip a
        -- 0-keyed entry, leaking the process and losing its result.
        procs[#procs + 1] = {
          name = string.format("%s shard %d", suite, i - 1),
          suite = suite,
          proc = proc,
          logfile = logfile,
          report = report,
          assigned = #shard.names
        }
      end
      return procs
    end

    -- The shard's own JUnit report decides its verdict: log text is evidence
    -- only, and wait() statuses are unreliable when many processes run
    -- concurrently (poller event bookkeeping under parallel waits). A shard
    -- that dies mid-run leaves no complete report, which counts as failed.
    local function shard_verdict(p)
      local content = readcontent(p.report)
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

    -- The child writes its console summary through a redirected stdout, so a
    -- read landing before the file is whole would under-count silently; retry
    -- briefly, then report the miss (the caller fails the shard).
    local function shard_counts(logfile)
      local content = ""
      for _ = 1, 40 do
        content = readcontent(logfile) or ""
        if content:find("assertions", 1, true) then
          break
        end
        os.sleep(50)
      end
      return parse_summary(content)
    end

    local function cpu_count()
      local n = tonumber(os.getenv("NUMBER_OF_PROCESSORS") or "")
      if n then
        return n
      end
      local info = os.cpuinfo()
      return info and info.ncpu or 4
    end

    local function clamp_shards(cores, divisor, max)
      return math.min(max, math.max(1, math.floor(cores / divisor)))
    end

    ---------------------------------------------------------------- --selftest

    local function selftest()
      local failed = 0
      local function check(label, ok, detail)
        if not ok then
          failed = failed + 1
        end
        print(string.format("  %s %s%s", ok and "ok  " or "FAIL", label, detail and (" - " .. detail) or ""))
      end

      local xml = "<MatchingTests>\n  <TestCase>\n    <Name>plain name</Name>\n  </TestCase>\n"
        .. "  <TestCase>\n    <Name>a &lt;b&gt; &amp; c, d</Name>\n  </TestCase>\n</MatchingTests>\n"
      local names = xml_case_names(xml)
      check("xml listing parse", #names == 2 and names[2] == "a <b> & c, d", table.concat(names, " | "))
      check(
        "console count parse",
        console_test_count("All available test cases:\n  1 test case\n") == 1
          and console_test_count("x\n723 test cases\n") == 723
      )

      check("comma escaped", escape_spec("a, b") == "a\\, b", escape_spec("a, b"))
      check("plain name untouched", escape_spec("plain name") == "plain name")
      check(
        "every reserved character is found",
        reserved_in("q?") == "?" and reserved_in("star*") == "*" and reserved_in("br[") == "["
          and reserved_in("br]") == "]" and reserved_in("tilde~") == "~" and reserved_in('quote"') == '"'
          and reserved_in("back\\slash") == "\\" and reserved_in("plain, name") == nil
      )
      check(
        "sound enumeration accepted",
        enumeration_error({"a, b", "c"}, 2) == nil and enumeration_error({}, 0) ~= nil
          and enumeration_error({"a"}, 2) ~= nil and enumeration_error({"a"}, nil) ~= nil
          and enumeration_error({"a", "a"}, 2) ~= nil and enumeration_error({"star*"}, 1) ~= nil
      )

      local costs = parse_durations("1.250 s: alpha\n0.500 s:   indented section\n2 s: beta\n")
      check(
        "durations parsed and folded",
        costs["alpha"] == 1.25 and costs["beta"] == 2 and costs["indented section"] == 0.5,
        table.concat({tostring(costs.alpha), tostring(costs.beta), tostring(costs["indented section"])}, "/")
      )

      -- A one-case shard prints the singular summary; the table form appears as
      -- soon as a case is skipped or fails. Both must yield the totals.
      local one_assertions, one_cases = parse_summary("All tests passed (3 assertions in 1 test case)\n")
      local many_assertions, many_cases = parse_summary("All tests passed (254 assertions in 12 test cases)\n")
      local table_assertions, table_cases = parse_summary(
        "test cases:  91 |  89 passed | 2 skipped\nassertions: 495 | 495 passed | 0 failed\n"
      )
      check("singular summary parsed", one_assertions == 3 and one_cases == 1)
      check("plural summary parsed", many_assertions == 254 and many_cases == 12)
      check("table summary parsed", table_assertions == 495 and table_cases == 91)
      check("absent summary reported", parse_summary("nothing here") == nil, "nil")

      local model = path.join(builddir, ".test-cost-selftest.txt")
      save_costs(model, {alpha = 1.25, beta = 2, ["com,ma"] = 0.5})
      local loaded = load_costs(model)
      os.rm(model)
      local loaded_size = #table.keys(loaded)
      check(
        "cost model round trip",
        loaded.alpha == 1.25 and loaded.beta == 2 and loaded["com,ma"] == 0.5,
        string.format("%d entries", loaded_size)
      )

      -- Balance fixture: one case three times as heavy as the rest, which LPT
      -- spreads so the heaviest shard stays under 1.3x the mean.
      local many = {}
      local many_costs = {}
      for i = 1, 40 do
        local name = string.format("case %02d", i)
        many[#many + 1] = name
        many_costs[name] = i == 1 and 3.0 or 1.0
      end
      local shards_a, fallback = partition(many, many_costs, 4)
      local shards_b = partition(many, many_costs, 4)
      local flat_a, flat_b = {}, {}
      local worst, total = 0, 0
      for i = 1, 4 do
        table.sort(shards_a[i].names)
        for _, name in ipairs(shards_a[i].names) do
          flat_a[#flat_a + 1] = name
        end
        for _, name in ipairs(shards_b[i].names) do
          flat_b[#flat_b + 1] = name
        end
        worst = math.max(worst, shards_a[i].cost)
        total = total + shards_a[i].cost
      end
      local ratio = total > 0 and worst / (total / 4) or 0
      check("partition is deterministic", table.concat(flat_a, "|") == table.concat(flat_b, "|"))
      check("partition covers every case once", assignment_error(many, shards_a) == nil, string.format("%d cases", #flat_a))
      check("partition balances a dominant case", ratio <= 1.3, string.format("max/mean %.2f", ratio))
      check("cost model fallback", fallback == 1.0, tostring(fallback))

      -- Sub-millisecond rows are the common case (Catch2 rounds durations to
      -- milliseconds), so a zero cost must not make a shard look free: those
      -- cases are priced at the median of the positive costs like any unknown.
      local mixed, mixed_costs = {}, {}
      for i = 1, 40 do
        local name = string.format("case %02d", i)
        mixed[#mixed + 1] = name
        mixed_costs[name] = (i % 4 == 0) and 0 or 1.0
      end
      local mixed_shards = partition(mixed, mixed_costs, 4)
      local mixed_counts, mixed_ratio, mixed_worst, mixed_total = {}, 0, 0, 0
      for i, shard in ipairs(mixed_shards) do
        mixed_counts[i] = #shard.names
        mixed_worst = math.max(mixed_worst, shard.cost)
        mixed_total = mixed_total + shard.cost
      end
      mixed_ratio = mixed_total > 0 and mixed_worst / (mixed_total / 4) or 0
      check(
        "zero costs are priced like unknown ones",
        mixed_shards[1].cost == 10.0 and mixed_ratio <= 1.3,
        string.format("cases %s, max/mean %.2f", table.concat(mixed_counts, "/"), mixed_ratio)
      )
      check(
        "unsound assignments rejected",
        assignment_error({"a", "b"}, {{names = {"a"}}}) ~= nil
          and assignment_error({"a", "b"}, {{names = {"a", "b"}}, {names = {"a"}}}) ~= nil
          and assignment_error({"a"}, {{names = {}}, {names = {}}}) ~= nil
          and assignment_error({"a", "b"}, {{names = {"a", "b"}}, {names = {}}}) ~= nil
      )

      if failed > 0 then
        os.raise(string.format("%d selftest check(s) failed", failed))
      end
      print("test-parallel selftest: all checks passed")
    end

    if option.get("selftest") then
      return selftest()
    end

    -------------------------------------------------------------------- driver

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
    if os.isdir(workdir) then
      os.rmdir(workdir)
    end
    makedirs(workdir)

    local suites = {
      {name = "unit", binary = tests_bin, shards = unit_shards},
      {name = "e2e", binary = e2e_bin, shards = e2e_shards}
    }

    -- Plan every suite first - enumeration, escaping, cost model, assignment -
    -- so no shard starts before the whole run can be accounted for.
    local plans = {}
    for _, suite in ipairs(suites) do
      local suite_dir = path.join(workdir, suite.name)
      makedirs(suite_dir)
      local names = enumerate(suite.binary, workdir, suite.name)
      preflight_escaping(suite.binary, names, workdir, suite.name)
      local costs = load_costs(cost_model_path(builddir, suite.name))
      plans[#plans + 1] = {
        name = suite.name,
        binary = suite.binary,
        dir = suite_dir,
        names = names,
        model_size = #table.keys(costs),
        shards = write_specs(names, suite_dir, suite.name, suite.shards, costs)
      }
    end

    for _, plan in ipairs(plans) do
      local counts, worst, total = {}, 0, 0
      for i, shard in ipairs(plan.shards) do
        counts[i] = string.format("%d", #shard.names)
        worst = math.max(worst, shard.cost)
        total = total + shard.cost
      end
      local ratio = total > 0 and worst / (total / #plan.shards) or 0
      cprint(
        "${dim}%s: %d shard(s) from %s, cases %s, max/mean cost %.2f",
        plan.name,
        #plan.shards,
        plan.model_size > 0 and string.format("a recorded cost model (%d cases)", plan.model_size) or "an equal split (no recorded cost yet)",
        table.concat(counts, "/"),
        ratio
      )
    end

    -- All shards spawn up front; processes run as they are created, so the
    -- wait pass below only harvests results.
    local procs = {}
    for _, plan in ipairs(plans) do
      for _, p in ipairs(spawn_shards(plan.binary, plan.shards, plan.dir, plan.name)) do
        procs[#procs + 1] = p
      end
    end

    local totals = {}
    for _, plan in ipairs(plans) do
      totals[plan.name] = {assertions = 0, cases = 0, enumerated = #plan.names}
    end
    local failures = {}
    for _, p in ipairs(procs) do
      -- Reap the process (its status is not trustworthy here), then judge the
      -- shard by its own report.
      p.proc:wait(-1)
      p.proc:close()
      local passed, reason = shard_verdict(p)
      if passed then
        local assertions, cases = shard_counts(p.logfile)
        if not assertions then
          passed, reason = false, "no console summary to read"
        elseif cases ~= p.assigned then
          passed, reason = false,
            string.format("executed %d case(s) of the %d assigned", cases, p.assigned)
        else
          totals[p.suite].assertions = totals[p.suite].assertions + assertions
          totals[p.suite].cases = totals[p.suite].cases + cases
        end
      end
      if not passed then
        p.reason = reason
        failures[#failures + 1] = p
      end
    end

    -- Record what this run measured for the next one to pack by, keyed by the
    -- enumeration: a duration row for a section is not a case, and a case that
    -- no longer exists must not linger in the model.
    for _, plan in ipairs(plans) do
      local costs = load_costs(cost_model_path(builddir, plan.name))
      local measured = {}
      for i, shard in ipairs(plan.shards) do
        local log = path.join(workdir, plan.name, string.format("shard-%d.log", i - 1))
        for name, seconds in pairs(parse_durations(readcontent(log) or "")) do
          if measured[name] == nil then
            measured[name] = seconds
          end
        end
      end
      local updated = {}
      for _, name in ipairs(plan.names) do
        updated[name] = measured[name] or costs[name]
      end
      save_costs(cost_model_path(builddir, plan.name), updated)
    end
    if #failures == 0 then
      for _, plan in ipairs(plans) do
        local t = totals[plan.name]
        cprint(
          "${bright green}%s: all tests passed (%d assertions in %d/%d test cases)",
          plan.name,
          t.assertions,
          t.cases,
          t.enumerated
        )
      end
      cprint("${dim}per-shard temp roots under %s (TMP/TEMP/TMPDIR)", workdir)
      return
    end

    for _, p in ipairs(failures) do
      cprint("${red}FAILED: %s - %s - log: %s", p.name, p.reason, p.logfile)
      local content = readcontent(p.logfile) or ""
      local shown = 0
      for line in content:gmatch("[^\n]+") do
        if line:find("FAILED", 1, true) then
          print("  " .. line)
          shown = shown + 1
          if shown >= 8 then
            break
          end
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
