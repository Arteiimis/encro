#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace fs = std::filesystem;

// Microseconds to seconds as double (ffmpeg timing math).
inline double microsToSeconds(std::uint64_t micros) {
  return static_cast<double>(micros) / 1'000'000.0;
}

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
  // Child stderr when stream merging is disabled; empty when merging is on
  // (the merged output already carries it). Never forwarded to our stderr.
  std::string stderrText;
};

auto exec2(std::string_view cmd) -> ExecResult;
auto exec2(std::string_view cmd, std::function<void(std::string_view)> const& onLine)
  -> ExecResult;
auto exec2(std::string_view cmd, bool mergeStdErr) -> ExecResult;

// Extracts a one-line failure reason for a failed child: the first line the
// classifier accepts (when given) — else the last non-empty line — from the
// separate stderr text when it carries anything, else from the retained
// merged-output capture; trimmed, capped at ~200 chars, with an
// "exit code N" fallback when neither carries a line.
auto extractFailureReason(
  std::string_view capturedOutput,
  std::string_view stderrText,
  int exitCode,
  std::function<bool(std::string_view)> const& acceptedLine = {}
) -> std::string;

bool readUserIpt(bool yesToAll, std::string_view prompt);

auto findFFprobe(std::optional<fs::path> const& installDir) -> std::optional<fs::path>;

auto findFFmpeg(std::optional<fs::path> const& installDir) -> std::optional<fs::path>;

std::string getUUID();
