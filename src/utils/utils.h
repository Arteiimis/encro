#pragma once

#include <boost/json.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/static_string.hpp>

#include <filesystem>
#include <optional>


namespace fs = std::filesystem;

struct ExecResult {
  int exitCode;
  std::string output;
};

auto exec2(std::string_view cmd) -> ExecResult;

bool readUserIpt(bool yesToAll, std::string_view prompt);

auto findFFprobe(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path>;

auto findFFmpeg(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path>;

auto find7zip() -> std::optional<fs::path>;

std::string getUUID();

auto getParamStr(
  const boost::program_options::variables_map& vm,
  std::string_view paramName
) -> std::string;
