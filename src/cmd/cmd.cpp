#include "cmd/cmd.h"

#include <string>
#include <utility>
#include <vector>

template<class Ty>
inline auto const pv = [] { return boost::program_options::value<Ty>(); };

template<class Ty>
inline auto const pvm =
  [] { return boost::program_options::value<Ty>()->multitoken(); };

template<class Ty>
auto pvDefault(Ty&& defaultValue) {
  return boost::program_options::value<Ty>()->default_value(
    std::forward<Ty>(defaultValue)
  );
}

using namespace std::literals;

auto commandLineInit(int argc, char* argv[]) -> CmdParseResult {
  auto general = po::options_description("General options");
  general.add_options()                                                            //
    ("help,h", "produce help message")                                             //
    ("verbose,v", "enable verbose output")                                         //
    ("verbose-echo,e", "echo verbose logs to console\n  (disable progress bars)")  //
    ("yes,y", "automatic yes to prompts")                                          //
    ;

  auto io = po::options_description("Input/Output options");
  io.add_options()                                                           //
    ("input,i", pv<std::string>(), "input file or directory path")           //
    ("inputs,I", pvm<std::vector<std::string>>(), "input video file paths")  //
    ("output,o", pv<std::string>(), "custom output directory path")          //
    ("output-format,f", pvDefault("mp4"s), "target format: mp4 or webp")     //
    ("flat", "flatten output names inside the output directory (default)")   //
    ("keep",
     "preserve relative input subdirectories inside the output directory")   //
    ("force-conflict-handling",
     pvDefault("y"s),
     "control collision-safe file names for unique flat outputs: y or n")  //
    ("recursive,r", "enable recursively search")                           //
    ;

  auto processing = po::options_description("Processing options");
  processing.add_options()                                                    //
    ("type,t", pvDefault("video"s), "process type: video(vid)|picture(pic)")  //
    ("jobs,j", pvDefault(10ull), "max parallel jobs (>=1, default=10)")       //
    ("ffmpeg-path,x", pv<std::string>(), "custom ffmpeg install path")        //
    ;

  auto fileop = po::options_description("File operation options");
  fileop.add_options()                                              //
    ("pack,p", "pack encoded video outputs into zip files")         //
    ("pack-only,z", "pack only: zip all files in input directory")  //
    ("overwrite,w", "overwrite existing files without prompt")      //
    ;

  auto all = po::options_description("Allowed options");
  all.add(general).add(io).add(processing).add(fileop);

  auto vm = po::variables_map{};
  auto error = std::optional<std::string>{};
  try {
    store(parse_command_line(argc, argv, all), vm);
    notify(vm);
  } catch (po::error const& ex) { error = ex.what(); }

  return {all, vm, error};
}
