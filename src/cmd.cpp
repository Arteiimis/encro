#include "cmd.h"

auto commandLineInit(int argc, char* argv[])
  -> std::pair<po::options_description, po::variables_map> {
  namespace po = boost::program_options;

  auto desc = po::options_description("Allowed options");
  desc.add_options()                                                          //
    ("help,h", "produce help message")                                        //
    ("input,i", po::value<std::string>(), "input file or directory path")     //
    ("output,o", po::value<std::string>(), "custom output directory path")    //
    ("ffmpeg-path,f", po::value<std::string>(), "custom ffmpeg binary path")  //
    ("verbose,v", "enable verbose output")                                    //
    ;

  auto vm = po::variables_map{};
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  return std::pair{desc, vm};
}
