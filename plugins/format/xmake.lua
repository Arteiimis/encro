task("format")
  set_category("plugin")
  set_menu({
    usage = "xmake format [options]",
    description = "Format C/C++ sources with clang-format",
    options = {
      {"k", "check", "k", nil, "Check formatting without modifying files"},
      {"s", "style", "v", nil, "clang-format style file or built-in style (default: D:/clangformat/.clang-format)"}
    }
  })

  on_run(function()
    local option = import("core.base.option")

    local tool = import("lib.detect.find_tool")("clang-format")
    assert(tool, "clang-format not found on PATH; install LLVM or add it to PATH")
    local clang_format = tool.program

    local check = option.get("check")
    local style = option.get("style") or "file:D:/clangformat/.clang-format"

    local files = {}
    for _, root in ipairs({"src", "tests"}) do
      for _, ext in ipairs({"c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "ipp", "inl"}) do
        for _, file in ipairs(os.files(path.join(root, "**." .. ext))) do
          table.insert(files, file)
        end
      end
    end
    table.sort(files)
    assert(#files > 0, "No C/C++ files found to format")

    local base_args = {"-style=" .. style}
    if check then
      table.insert(base_args, "--dry-run")
      table.insert(base_args, "--Werror")
    else
      table.insert(base_args, "-i")
    end

    -- 一次调用处理多个文件，按命令行长度分块，避免超过 Windows 32K 限制
    local chunks = {}
    local chunk, chunk_len = {}, 0
    local function flush()
      if #chunk > 0 then
        table.insert(chunks, chunk)
        chunk, chunk_len = {}, 0
      end
    end
    for _, file in ipairs(files) do
      if chunk_len + #file > 20000 then
        flush()
      end
      table.insert(chunk, file)
      chunk_len = chunk_len + #file
    end
    flush()

    -- 跑完全部块再统一报错，让 check 模式一次列出所有违规文件
    local failed = false
    for _, chunk in ipairs(chunks) do
      local ret = try {
        function()
          return os.execv(clang_format, table.join(base_args, chunk))
        end
      }
      if not ret or ret ~= 0 then
        failed = true
      end
    end

    if failed then
      error(string.format("clang-format %s failed; %d file(s) processed", check and "check" or "apply", #files))
    end
    print(string.format(
      check and "clang-format check passed for %d files" or "clang-format applied to %d files",
      #files
    ))
  end)
task_end()
