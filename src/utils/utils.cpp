#include "utils/utils.h"

#include <boost/lexical_cast.hpp>
#include <boost/process/v1.hpp>
#include <boost/uuid.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <print>

auto exec2(std::string_view cmd) -> ExecResult {
  namespace bp = boost::process::v1;

  spdlog::debug("Executing command: {}", cmd);

  auto pipeStream = bp::ipstream{};
  auto process = bp::child(cmd.data(), bp::std_out > pipeStream);
  auto line = std::string{};
  auto result = std::string{};

  while (std::getline(pipeStream, line)) {
    std::format_to(std::back_inserter(result), "{}\n", line);
  }
  process.wait();

  return {process.exit_code(), result};
}

bool readUserIpt(bool yesToAll, std::string_view prompt) {
  if (yesToAll) { return true; }

  if (!prompt.empty()) { std::print("{}", prompt); }

  auto response = 'n';
  auto input = std::string{};
  std::getline(std::cin, input);
  if (!input.empty()) { std::istringstream(input) >> response; }

  return response == 'y' || response == 'Y';
}

auto findFFprobe(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path> {
  const auto systemFFprobeAvailable = exec2("ffprobe -version").exitCode == 0;

  if (!installDir.has_value() && systemFFprobeAvailable) {
    return fs::path{"ffprobe"};
  }

  if (!installDir.has_value() || !fs::is_directory(installDir.value())) {
    return std::nullopt;
  }

  auto pathIter = fs::recursive_directory_iterator{installDir.value()};

  for (const auto& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffprobe") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
}

auto findFFmpeg(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path> {
  const auto systemFFmpegAvailable = exec2("ffmpeg -version").exitCode == 0;

  if (!installDir.has_value() && systemFFmpegAvailable) {
    return fs::path{"ffmpeg"};
  }

  if (!installDir.has_value() || !fs::is_directory(installDir.value())) {
    return std::nullopt;
  }

  auto pathIter = fs::recursive_directory_iterator{installDir.value()};

  for (const auto& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffmpeg") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
}

std::string getUUID() {
  return boost::lexical_cast<std::string>(boost::uuids::random_generator{}());
}

auto getParamStr(
  const boost::program_options::variables_map& vm,
  std::string_view paramName
) -> std::string {
  return boost::trim_copy(vm.at(paramName.data()).as<std::string>());
}
