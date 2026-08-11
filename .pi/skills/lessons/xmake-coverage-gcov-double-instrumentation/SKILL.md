---
name: xmake-coverage-gcov-double-instrumentation
description: Coverage mode builds hang or are extremely slow in exec2/fork paths (Linux/WSL); compile_commands.json contains both --coverage (gcov) and -fprofile-instr-generate (LLVM); strace shows the forked child writing .gcda files before exec.
---

# xmake coverage mode: gcov double-instrumentation

## Symptom

- `xmake f -m coverage` builds, but child processes spawned via exec2 (boost::process::v2) hang for seconds-to-forever on Linux/WSL — the parent blocks waiting for the child's exec status pipe.
- `compile_commands.json` shows BOTH `--coverage` and `-fprofile-instr-generate` in every compile command.
- `strace -f` on the hanging test: the forked child runs `openat(...*.gcda)` + `fcntl(F_SETLKW)` + mmap for hundreds of files BEFORE `execve(sh)` — it is dumping gcov data in the `pthread_atfork` child hook.
- CI's coverage job fails with flaky exec2 output loss (`REQUIRE(lines.size() == 2)` → `0 == 2`) while debug/release jobs pass.

## Root cause

xmake's built-in `add_rules("mode.coverage")` rule injects `--coverage` (gcov instrumentation → `.gcda` files, with an atfork dump hook that runs in EVERY forked child before exec). Adding LLVM flags (`-fprofile-instr-generate -fcoverage-mapping`) on top produces DOUBLE instrumentation. The gcov atfork dump delays the forked child's exec by seconds, which (a) hangs the parent waiting for exec status under slow/contended filesystems, and (b) massively widens any pipe-reader race in exec2, making it reproduce nearly always in coverage mode only.

## Fix

Do NOT use the built-in `mode.coverage` rule with manual LLVM flags. Remove it and configure coverage manually (LLVM-only):

```lua
add_rules("mode.debug", "mode.release", "mode.releasedbg")  -- no mode.coverage

if is_mode("coverage") then
  set_policy("build.optimization.lto", false)
  set_policy("build.ccache", false)
  set_symbols("debug")
  set_optimize("none")
  add_cxxflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
  add_ldflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
end
```

Verify by grepping `build/compile_commands.json` — only `-fprofile-instr-generate` must remain.

## Verify

- `grep -o "\-\-coverage" build/compile_commands.json` → no output.
- Run the tests binary; exec2-based tests return instantly instead of hanging.
- Under load (`yes > /dev/null` × nproc in parallel), 100 runs of the CRLF test: 0 failures, 0 hangs.
