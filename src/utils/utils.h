#pragma once

#include <boost/program_options/variables_map.hpp>

#include <filesystem>
#include <functional>
#include <optional>

namespace fs = std::filesystem;

struct ExecResult {
  int exitCode;
  std::string output;
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

auto findFFprobe(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path>;

auto findFFmpeg(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path>;

std::string getUUID();

auto getParamStr(
  boost::program_options::variables_map const& vm,
  std::string_view paramName
) -> std::string;
