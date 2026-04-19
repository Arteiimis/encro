#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace e2e {

struct ProcessResult {
  int exitCode = -1;
  std::string stdoutText;
  std::string stderrText;
};

struct FakeToolchain {
  fs::path root;
  fs::path ffmpegPath;
  fs::path ffprobePath;
};

auto encroBinaryPath() -> fs::path;

auto fakeMediaToolBinaryPath() -> fs::path;

auto resolveToolOnPath(std::string_view executable) -> std::optional<fs::path>;

auto runProcess(
  fs::path const& executable,
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir = std::nullopt,
  std::map<std::string, std::string> const& environment = {}
) -> ProcessResult;

auto runEncro(
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir = std::nullopt,
  std::map<std::string, std::string> const& environment = {}
) -> ProcessResult;

auto installFakeToolchain(fs::path const& root) -> FakeToolchain;

auto writeTextFile(fs::path const& path, std::string_view content = "x") -> void;

auto listZipEntries(fs::path const& zipPath) -> std::vector<std::string>;

}  // namespace e2e
