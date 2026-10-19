task("size")
  set_category("plugin")
  set_menu({
    usage = "xmake size [options]",
    description = "Analyze binary size: section overview, or per-object breakdown via PDB",
    options = {
      {"d", "breakdown", "k", nil, "Per-object breakdown via the symbol file"},
      {"t", "target", "kv", "encro", "Target binary name (default: encro)"},
      {"m", "mode", "kv", "release", "Build mode to analyze (default: release)"},
      {"n", "top", "kv", "20", "Rows to show per section (default: 20)"}
    }
  })

  on_run(function()
    local option = import("core.base.option")
    local config = import("core.project.config")
    config.load()

    local platform = config.plat() or os.host()
    local builddir = config.builddir({absolute = true})
    local target = option.get("target")
    local mode = option.get("mode")
    local top = tonumber(option.get("top"))
    local ext = platform == "windows" and ".exe" or ""

    local exe = path.join(builddir, platform, os.arch(), mode, target .. ext)
    if not os.exists(exe) then
      os.raise(string.format("binary not found: %s (build it first)", exe))
    end

    local function find_tool(name)
      local tool = import("lib.detect.find_tool")(name)
      assert(tool, string.format("%s not found on PATH; install LLVM or add it to PATH", name))
      return tool.program
    end

    local function run(cmd, args, opts)
      local ret = os.execv(cmd, args, table.join(opts or {}, {try = true}))
      if not ret or ret ~= 0 then
        os.raise(string.format("command failed: %s %s", cmd, table.concat(args, " ")))
      end
    end

    -- 1) section overview (instant, no rebuild)
    run(find_tool("llvm-size"), {"-A", exe})

    if not option.get("breakdown") then
      return
    end

    -- 2) per-object breakdown: PDB section contributions (exact bytes). The
    -- symbol file is written beside the binary in the modes this analyses, so a
    -- missing one means the build carried no symbols, not that a rebuild is due.
    local pdb = exe:gsub("%.exe$", ".pdb")
    if not os.exists(pdb) then
      os.raise(string.format(
        "no symbol file next to %s; build it with debug symbols to get the per-object breakdown",
        exe
      ))
    end

    local failed = nil
    try {
      function()
        local pdbutil = find_tool("llvm-pdbutil")

        local mods_out = os.iorunv(pdbutil, {"dump", "--modules", pdb})
        local names = {}
        for line in mods_out:gmatch("[^\r\n]+") do
          local mod, name = line:match("Mod (%d+) %| `(.+)`")
          if mod then
            names[tonumber(mod)] = name:gsub("\\", "/"):match("([^/]+)$")
          end
        end

        local contribs_out = os.iorunv(pdbutil, {"dump", "--section-contribs", pdb})
        local text, rdata = {}, {}
        for line in contribs_out:gmatch("[^\r\n]+") do
          local sec, mod, size = line:match("SC%[([%w%.%$]+)%]%s+%| mod = (%d+), .-size = (%d+),")
          if sec and mod then
            local m, s = tonumber(mod), tonumber(size)
            local tbl = (sec == ".text" and text) or (sec == ".rdata" and rdata)
            if tbl then
              tbl[m] = (tbl[m] or 0) + s
            end
          end
        end

        local function dump(label, tbl)
          local rows = {}
          local total = 0
          for m, size in pairs(tbl) do
            table.insert(rows, {size, names[m] or ("mod " .. m)})
            total = total + size
          end
          table.sort(rows, function(a, b)
            return a[1] > b[1]
          end)
          print(string.format("\n== %s per object (top %d) ==", label, top))
          for i = 1, math.min(top, #rows) do
            print(string.format("%9.1f KB  %s", rows[i][1] / 1024, rows[i][2]))
          end
          print(string.format("%9.1f KB  (total across %d objects)", total / 1024, #rows))
        end

        dump(".text", text)
        dump(".rdata", rdata)
      end,
      catch {
        function(e)
          failed = e
        end
      }
    }

    if failed then
      cprint("${red}size analysis failed: %s", failed)
      os.exit(1)
    end
  end)
task_end()
