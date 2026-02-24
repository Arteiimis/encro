#pragma once

#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace std::literals;

struct CmdParseResult {
  po::options_description desc;
  po::variables_map vm;
};

template<class Ty>
const auto pv = boost::program_options::value<Ty>();

template<class Ty>
auto pvDefault(Ty&& defaultValue) {
  return boost::program_options::value<Ty>()->default_value(
    std::forward<Ty>(defaultValue)
  );
}

constexpr auto commandLineInit(int argc, char* argv[]) -> CmdParseResult {
  auto general = po::options_description("General options");
  general.add_options()                     //
    ("help,h", "produce help message")      //
    ("verbose,v", "enable verbose output")  //
    ("yes,y", "automatic yes to prompts")   //
    ;

  auto io = po::options_description("Input/Output options");
  io.add_options()                                                           //
    ("input,i", pv<std::string>, "input file or directory path")             //
    ("output,o", pv<std::string>, "custom output directory path")            //
    ("output-format,ofmt", pvDefault("mp4"s), "target format: mp4 or webp")  //
    ("recursive,R", "recursively search for media files in directories")     //
    ;

  auto processing = po::options_description("Processing options");
  processing.add_options()                                             //
    ("type,t", pvDefault("video"s), "process type: video or picture")  //
    ("ffmpeg-path,f", pv<std::string>, "custom ffmpeg install path")   //
    ;

  auto fileop = po::options_description("File operation options");
  fileop.add_options()                                        //
    ("overwrite", "overwrite existing files without prompt")  //
    ;

  auto all = po::options_description("Allowed options");
  all.add(general).add(io).add(processing).add(fileop);

  auto vm = po::variables_map{};
  store(parse_command_line(argc, argv, all), vm);
  notify(vm);

  return {all, vm};
}
