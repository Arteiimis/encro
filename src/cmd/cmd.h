#pragma once

#include <boost/program_options.hpp>

namespace po = boost::program_options;

struct CmdParseResult {
  po::options_description desc;
  po::variables_map vm;
};

auto commandLineInit(int argc, char* argv[]) -> CmdParseResult;
