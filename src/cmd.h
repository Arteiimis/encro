#pragma once

#include <boost/program_options.hpp>

namespace po = boost::program_options;

auto commandLineInit(int argc, char* argv[])
  -> std::pair<po::options_description, po::variables_map>;
