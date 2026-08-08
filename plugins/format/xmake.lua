task("format")
  set_category("plugin")
  set_menu({
    usage = "xmake format [options]",
    description = "Format C/C++ sources with clang-format",
    options = {
      {"k", "check", "k", nil, "Check formatting without modifying files"}
    }
  })

  on_run(function()
    local option = import("core.base.option")

    local style_file = "D:/clangformat/.clang-format"
    assert(os.isfile(style_file), "clang-format config not found: " .. style_file)

    local patterns = {}
    local roots = {"src", "tests"}
    local exts = {"c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "ipp", "inl"}
    for _, root in ipairs(roots) do
      for _, ext in ipairs(exts) do
        table.insert(patterns, path.join(root, "**." .. ext))
      end
    end

    local files = {}
    local seen = {}
    for _, pattern in ipairs(patterns) do
      for _, file in ipairs(os.files(pattern)) do
        if not seen[file] then
          seen[file] = true
          table.insert(files, file)
        end
      end
    end

    table.sort(files)
    assert(#files > 0, "No C/C++ files found to format")

    local args = {string.format("-style=file:%s", style_file)}
    if option.get("check") then
      table.insert(args, "--dry-run")
      table.insert(args, "--Werror")
    else
      table.insert(args, "-i")
    end

    for _, file in ipairs(files) do
      local file_args = table.clone(args)
      table.insert(file_args, file)
      os.execv("clang-format", file_args)
    end

    if option.get("check") then
      print(string.format("clang-format check passed for %d files", #files))
    else
      print(string.format("clang-format applied to %d files", #files))
    end
  end)
task_end()