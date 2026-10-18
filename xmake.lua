add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "./build" })

-- The release version is declared once, here. set_version() below and the
-- generated version header both read this variable, so no second literal
-- exists to drift from it.
local encroVersion = "2.0.0"
set_version(encroVersion)

set_languages("c++26")

if is_plat("windows") then
  set_toolchains("clang-cl")
  set_toolset("ld", "lld-link")
  set_toolset("ar", "llvm-ar")
else
  set_toolchains("clang")
  -- clang + GNU ld 的 thin-LTO 链接不可靠（catch2/boost 符号被裁剪）；
  -- 统一 lld（与 Windows 的 lld-link 对齐，xmake 3 对包默认注入 lto）
  add_ldflags("-fuse-ld=lld", {force = true})
end

add_cxxflags("-ftrivial-auto-var-init=pattern")

add_plugindirs("./plugins/")

if is_mode("coverage") then
  -- 不用内置 mode.coverage 规则：它的 --coverage（gcov 插桩）会在 fork 后
  -- 逐文件 dump .gcda，拖慢/卡住 exec2 的子进程；这里只用 LLVM 插桩。
  set_policy("build.ccache", false)
  set_symbols("debug")
  set_optimize("none")
  add_cxxflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
  add_ldflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
end

if is_mode("releasedbg") then
  set_strip("none")
  add_runenvs("ASAN_OPTIONS", "poison_history_size=4096")
  set_policy("build.sanitizer.address", true)
end

if is_plat("windows") then
  add_defines("NOMINMAX")
  add_defines("WIN32_LEAN_AND_MEAN")
  -- C++26 hardened MSVC STL: precondition violations trap (ud2 under clang-cl,
  -- interceptable by the crash handler) instead of UB. See src/infra/crash_runtime.cpp.
  add_defines("_MSVC_STL_HARDENING=1")
else
  -- clang + libstdc++: cheap precondition checks (no ABI break); violations print
  -- a reason and abort, surfacing through the crash handler as SIGABRT.
  add_defines("_GLIBCXX_ASSERTIONS=1")
end

if is_mode("release") and is_plat("windows") then
  -- Crash stacks must resolve: emit PDBs next to release binaries so crash
  -- records show module!function instead of module+offset. The symbol file is
  -- kept out of the runtime archive by the xpack hook below and published as
  -- its own asset instead.
  set_symbols("debug")
end

-- SPDLOG_ACTIVE_LEVEL per build mode (D-14)
-- release/releasedbg: strip TRACE and DEBUG at compile time
-- debug/coverage: keep all levels for development
if is_mode("release") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
elseif is_mode("releasedbg") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
elseif is_mode("debug") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
elseif is_mode("coverage") then
  add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
end

add_requires("boost[process,stacktrace,asio,context,date_time,json]")
add_requires("cli11")
add_requires("thread-pool")
add_requires("spdlog[fmt_external]")
add_requires("fmt")
add_requires("indicators")
add_requires("libzippp")
-- add_requireconfs("libzippp.libzip", {configs = {toolchains = "clang"}})
-- Header-only HTTP server for the downloader tests; the shipped binary
-- itself downloads via the platform curl (no OpenSSL linkage).
add_requires("cpp-httplib v0.53.1")
-- Local GPU-flavor ORT package (build-repo/): the xrepo one drags a `cuda`
-- build dep we don't want (CUDA/cuDNN DLLs are runtime concerns for us) and
-- it is unsupported under MSYS-shell package envs. Its prebuilt binaries are
-- win-x64-only, so non-Windows hosts skip it and build the tagger stub.
add_repositories("encro-repo build-repo")
if is_plat("windows") then
  add_requires("onnxruntime-gpu 1.22.1")
end
add_requires("catch2")

-- Copies the shared ORT runtime DLLs next to binaries that link them:
-- direct exe execution does not get xmake's package PATH injection, and a
-- stale onnxruntime.dll from System32 or PATH would mismatch the headers.
-- Only the ORT core plus its CUDA EP plugin go next to the exes: the EP's
-- cuDNN/cuBLAS dependencies resolve at runtime through
-- ensureGpuRuntimePaths (%LOCALAPPDATA%/encro/lib self-install + the user's
-- CUDA on PATH), so copying the package's full ~1.9GB CUDA stack into
-- build/ would just duplicate those.
local function copyOrtRuntimeDlls(target)
  local pkg = target:pkg("onnxruntime-gpu")
  if pkg == nil then return end
  local bindir = path.join(pkg:installdir(), "bin")
  if not os.isdir(bindir) then return end
  for _, dll in ipairs({
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
    "onnxruntime_providers_cuda.dll",
  }) do
    local src = path.join(bindir, dll)
    if os.isfile(src) then os.cp(src, target:targetdir()) end
  end
end

-- All src/ code compiles exactly once into this static lib, linked by both
-- encro and tests. Per-target compilation duplicated ~66 TUs with per-target
-- command lines (test defines vs LTO), which also defeated the build cache
-- (its key is the full command line).
target("encro_core")
  set_kind("static")
  set_default(false)

  if is_mode("release") then
    set_policy("build.optimization.lto", true)
  end

  add_packages("boost", "thread-pool", "indicators", "libzippp", "fmt", "spdlog", "cli11", {public = true})
  if is_plat("windows") then
    add_packages("onnxruntime-gpu", {public = true})
    add_syslinks("dbghelp", "shell32", "ole32", {public = true})
  else
    add_syslinks("dl", {public = true})
  end

  add_includedirs("src", {public = true})

  -- The version has one declaration: `encroVersion`, above. set_version()
  -- and the generated header both read it, and app_entry.cpp prints the
  -- macro, never a literal.
  set_configvar("ENCRO_VERSION", encroVersion, {quote = true})
  add_configfiles("src/app/version.h.in", {prefixdir = "app"})
  add_includedirs("$(builddir)", {public = true})

  add_files("src/**.cpp|main.cpp")
  -- ONNX inference links win-x64-only ORT binaries, and onnx_runtime.cpp plus
  -- every engine over it include the ORT headers; other hosts get the
  -- engine_factory stubs instead.
  if not is_plat("windows") then
    remove_files("src/tagger/onnx_*.cpp")
  end
target_end()

target("encro")
  set_kind("binary")

  if is_mode("release") then
    set_policy("build.optimization.lto", true)
  end

  add_deps("encro_core")

  add_files("src/main.cpp")
  after_build(copyOrtRuntimeDlls)
target_end()

target("encro_e2e_tool")
  set_kind("binary")
  set_default(false)

  add_files("tests/e2e/fake_media_tool.cpp")
target_end()


-- Injects the absolute path of the fake media tool binary as a compile-time
-- define, so unit and e2e test binaries can spawn it directly.
local function injectFakeToolDefine(target)
  local dep = target:dep("encro_e2e_tool")
  if dep then
    local exe = path.absolute(dep:targetfile())
    target:add("defines", "FAKE_TOOL_EXE_PATH=\"" .. exe:gsub("\\", "\\\\") .. "\"")
  end
end

-- Injects the repo's tests/ source root so in-suite meta checks can scan
-- test sources from the built binary (test processes don't know the repo
-- layout at runtime).
local function injectTestSourceDirDefine(target)
  local testsDir = path.absolute(path.join(os.projectdir(), "tests"))
  target:add("defines", "ENCRO_TEST_SOURCE_DIR=\"" .. testsDir:gsub("\\", "\\\\") .. "\"")
end

target("tests")
  set_kind("binary")
  set_default(false)

  -- Shared deps (boost/spdlog/... + src includes) arrive via encro_core.
  add_packages("catch2", "cpp-httplib")
  add_includedirs("tests")
  add_files("tests/*.cpp")
  add_files("tests/app/*.cpp")
  add_files("tests/batch/*.cpp")
  add_files("tests/infra/*.cpp")
  add_files("tests/organize/*.cpp")
  add_files("tests/picture/*.cpp")
  add_files("tests/preview/*.cpp")
  add_files("tests/tagger/*.cpp")
  -- Constructs OnnxTagger directly; windows-only like the engine it drives.
  if not is_plat("windows") then
    remove_files("tests/tagger/real_model_tests.cpp")
  end
  add_files("tests/video/*.cpp")

  -- Unit tests spawn the fake media tool exe directly (no cmd.exe layer).
  add_deps("encro_e2e_tool", "encro_core")
  after_load(function(target)
    injectFakeToolDefine(target)
    injectTestSourceDirDefine(target)
  end)
  after_build(copyOrtRuntimeDlls)
target_end()

target("e2e_tests")
  set_kind("binary")
  set_default(false)

  add_packages("catch2", "boost", "libzippp", "fmt", "spdlog", "cli11")
  add_includedirs("tests")
  if is_plat("windows") then
    add_syslinks("dbghelp", "shell32", "ole32")
  else
    add_syslinks("dl")
  end

  add_deps("encro", "encro_e2e_tool")
  after_load(injectFakeToolDefine)
  after_build(copyOrtRuntimeDlls)
  add_files("tests/e2e/*.cpp|fake_media_tool.cpp")
target_end()

includes("@builtin/xpack")

-- xpack copies a target's symbol file into bin/ unconditionally and the DSL
-- has no declarative exclude, so the runtime archive is trimmed here and the
-- symbols ship as their own asset instead. The VC redistributable DLLs are
-- added here too: they must come from the Visual Studio installation that
-- built the binary, so their version follows the build rather than the target
-- machine.
xpack("encro")
  set_formats("zip", "targz")
  -- $(version) is the single declaration above; $(arch) and $(plat) are
  -- whatever this platform's toolchain reports (x64 on Windows, x86_64 on
  -- Linux), never a normalised value.
  set_basename("encro-$(version)-$(arch)-$(plat)")
  set_title("Encro")
  set_author("Artemiss")
  set_description("encro: Universal video encoder/converter/packer")
  set_homepage("https://github.com/Arteiimis/encro")
  add_targets("encro")
  -- licensefile is a specvar consumed only by the installer generators, so a
  -- zip would carry neither the licence nor the README without these.
  add_installfiles("LICENSE", "README.md")

  after_installcmd(function (package, batchcmds)
    -- Unconditional: batchcmds only queues commands, so the file does not
    -- exist yet at registration time — it is ordered after the copy that
    -- places it. Removing a path that ends up absent is tolerated, which is
    -- how xmake's own uninstall commands use it.
    batchcmds:rm(package:installdir("bin/encro.pdb"), {emptydirs = true})

    if is_plat("windows") then
      -- The Universal C Runtime (ucrtbase.dll, api-ms-win-crt-*) ships with
      -- Windows, so it is deliberately absent. These five come from the
      -- Visual C++ redistributable and are missing from a machine that never
      -- installed one: encro.exe imports MSVCP140, MSVCP140_ATOMIC_WAIT,
      -- VCRUNTIME140 and VCRUNTIME140_1, and onnxruntime.dll imports
      -- MSVCP140, MSVCP140_1, VCRUNTIME140 and VCRUNTIME140_1.
      local dlls = {
        "msvcp140.dll",
        "msvcp140_1.dll",
        "msvcp140_atomic_wait.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
      }

      local candidates = {}
      local toolsRedistDir = os.getenv("VCToolsRedistDir")
      if toolsRedistDir then
        table.insert(candidates, toolsRedistDir)
      end
      local pf86 = os.getenv("ProgramFiles(x86)") or "C:\\Program Files (x86)"
      local vswhere = path.join(pf86, "Microsoft Visual Studio/Installer/vswhere.exe")
      if os.isfile(vswhere) then
        local vsPath = os.iorunv(vswhere, {"-latest", "-products", "*", "-property", "installationPath"})
        vsPath = vsPath and vsPath:trim()
        if vsPath and #vsPath > 0 then
          local redistRoot = path.join(vsPath, "VC/Redist/MSVC")
          for _, verdir in ipairs(os.dirs(path.join(redistRoot, "*"))) do
            table.insert(candidates, path.join(verdir, "x64"))
          end
        end
      end

      local cruit = nil
      for _, candidate in ipairs(candidates) do
        for _, crtdir in ipairs(os.dirs(path.join(candidate, "Microsoft.VC*.CRT"))) do
          cruit = crtdir
          break
        end
        if cruit then break end
      end
      if not cruit then
        raise("no Visual C++ redistributable directory found; the package would " ..
              "not start on a machine that never had one (set VCToolsRedistDir)")
      end
      for _, dll in ipairs(dlls) do
        local src = path.join(cruit, dll)
        if not os.isfile(src) then
          raise(dll .. " is missing from " .. cruit)
        end
        batchcmds:cp(src, package:installdir("bin"))
      end
    end
  end)
xpack_end()

