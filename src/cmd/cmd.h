#pragma once

#include <boost/program_options.hpp>

#include <optional>
#include <string>

namespace po = boost::program_options;

struct CmdParseResult {
  po::options_description desc;
  po::variables_map vm;
  std::optional<std::string> error;
};

auto commandLineInit(int argc, char* argv[]) -> CmdParseResult;
