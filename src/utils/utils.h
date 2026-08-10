#pragma once

#include <filesystem>
#include <functional>
#include <optional>

namespace fs = std::filesystem;

// Windows ffmpegPath may be a compound command (cmd.exe /d /c call "...")
// that must not be quoted as a single executable; posix needs quotes for
// paths with spaces and its exe resolution strips them.
inline auto quoteToolPath(fs::path const& toolPath) -> std::string {
#if defined(_WIN32)
  return toolPath.string();
#else
  return std::format("\"{}\"", toolPath.string());
#endif
}

struct ExecResult {
  int exitCode;
  std::string output;
  std::optional<int> pid;
};

auto exec2(std::string_view cmd) -> ExecResult;
auto exec2(std::string_view cmd, std::function<void(std::string_view)> const& onLine)
  -> ExecResult;
auto exec2(std::string_view cmd, bool mergeStdErr) -> ExecResult;
auto exec2(
  std::string_view cmd,
  std::function<void(std::string_view)> const& onLine,
  bool mergeStdErr
) -> ExecResult;

bool readUserIpt(bool yesToAll, std::string_view prompt);

auto findFFprobe(std::optional<fs::path> const& installDir) -> std::optional<fs::path>;

auto findFFmpeg(std::optional<fs::path> const& installDir) -> std::optional<fs::path>;

std::string getUUID();
