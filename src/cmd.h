#pragma once

#include <boost/program_options.hpp>

namespace po = boost::program_options;

struct CmdParserResult {
  po::options_description desc;
  po::variables_map vm;
};

constexpr auto commandLineInit(int argc, char* argv[]) -> CmdParserResult {
  namespace po = boost::program_options;

  // 通用选项 (General options)
  auto general = po::options_description("General options");
  general.add_options()                     //
    ("help,h", "produce help message")      //
    ("verbose,v", "enable verbose output")  //
    ("yes,y", "automatic yes to prompts")   //
    ;

  // 输入/输出选项 (Input/Output options)
  auto io = po::options_description("Input/Output options");
  io.add_options()                                                            //
    ("input,i", po::value<std::string>(), "input file or directory path")     //
    ("output,o", po::value<std::string>(), "custom output directory path")    //
    ("output-format,ofmt", po::value<std::string>(), "custom output format")  //
    ("recursive,R", "recursively search for video files in directories")      //
    ;

  // 处理选项 (Processing options)
  auto processing = po::options_description("Processing options");
  processing.add_options()                                                    //
    ("type,t", po::value<std::string>(), "process type: video or picture")    //
    ("ffmpeg-path,f", po::value<std::string>(), "custom ffmpeg binary path")  //
    ;

  // 文件操作选项 (File operation options)
  auto fileop = po::options_description("File operation options");
  fileop.add_options()                                        //
    ("overwrite", "overwrite existing files without prompt")  //
    ;

  // 组合所有选项组
  po::options_description all("Allowed options");
  all.add(general).add(io).add(processing).add(fileop);

  auto vm = po::variables_map{};
  po::store(po::parse_command_line(argc, argv, all), vm);
  po::notify(vm);

  return {all, vm};
}
