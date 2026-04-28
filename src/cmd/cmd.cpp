#include "cmd/cmd.h"

#include "infra/console_width.h"

#include <string>
#include <utility>
#include <vector>

template<class Ty>
inline auto const pv = [] { return boost::program_options::value<Ty>(); };

template<class Ty>
inline auto const pvm = [] { return boost::program_options::value<Ty>()->multitoken(); };

template<class Ty>
auto pvDefault(Ty&& defaultValue) {
  return boost::program_options::value<Ty>()->default_value(
    std::forward<Ty>(defaultValue)
  );
}

using namespace std::literals;

namespace {

struct HelpTextLayout {
  unsigned lineLength;
  unsigned minDescriptionLength;
};

auto resolveHelpTextLayout() -> HelpTextLayout {
  auto const lineLength = static_cast<unsigned>(consolewidth::resolveColumns({
    .defaultColumns = po::options_description::m_default_line_length,
    .minColumns = 40,
    .maxColumns = 120,
  }));

  return {
    .lineLength = lineLength,
    .minDescriptionLength = lineLength / 2,
  };
}

}  // namespace

auto commandLineInit(int argc, char* argv[]) -> CmdParseResult {
  auto const layout = resolveHelpTextLayout();

  auto general = po::options_description(
    "General options",
    layout.lineLength,
    layout.minDescriptionLength
  );
  general.add_options()                                                                //
    ("help,h", "produce help message")                                                 //
    ("verbose,v", "enable verbose output")                                             //
    ("verbose-echo,e", "echo verbose logs to console (disable progress bars)")         //
    ("full-progress,F",
     "show full progress with per-worker encoding bars and per-archive packing bars")  //
    ("color", pvDefault("auto"s), "terminal colors: auto, always, never")              //
    ("yes,y", "automatic yes to prompts")                                              //
    ;

  auto io = po::options_description(
    "Input/Output options",
    layout.lineLength,
    layout.minDescriptionLength
  );
  io.add_options()                                                           //
    ("input,i", pv<std::string>(), "input file or directory path")           //
    ("inputs,I", pvm<std::vector<std::string>>(), "input video file paths")  //
    ("output,o",
     pv<std::string>(),
     "custom output directory path\n"
     "  aliases: + or input:// for input root, = or common:// for common root")  //
    ("state-file", pv<std::string>(), "custom job state file path")              //
    ("output-format,f", pvDefault("mp4"s), "target format: mp4 or webp")         //
    ("flat", "flatten output names inside the output directory (default)")       //
    ("keep",
     "preserve relative input subdirectories inside the output directory")       //
    ("force-conflict-handling",
     pvDefault("y"s),
     "control collision-safe file names for unique flat outputs: y or n")          //
    ("folder-summary", "enable picture-mode folder summary images in flat packs")  //
    ("recursive,r", "enable recursively search")                                   //
    ;

  auto processing = po::options_description(
    "Processing options",
    layout.lineLength,
    layout.minDescriptionLength
  );
  processing.add_options()                                                    //
    ("type,t", pvDefault("video"s), "process type: video(vid)|picture(pic)")  //
    ("jobs,j", pvDefault(10ull), "max parallel jobs (>=1, default=10)")       //
    ("resume", "resume previous unfinished job state when available")         //
    ("restart", "ignore previous job state and start a fresh run")            //
    ("ffmpeg-path,x", pv<std::string>(), "custom ffmpeg install path")        //
    ("compress", "enable JPEG compression during picture processing")         //
    ("image-quality,q",
     pv<int>(),
     "JPEG compression quality (2-31, default=5, lower=better)")  //
    ;

  auto fileop = po::options_description(
    "File operation options",
    layout.lineLength,
    layout.minDescriptionLength
  );
  fileop.add_options()                                              //
    ("pack,p", "pack encoded video outputs into zip files")         //
    ("pack-only,z", "pack only: zip all files in input directory")  //
    ("overwrite,w", "overwrite existing files without prompt")      //
    ;

  auto all = po::options_description(
    "Allowed options",
    layout.lineLength,
    layout.minDescriptionLength
  );
  all.add(general).add(io).add(processing).add(fileop);

  auto vm = po::variables_map{};
  auto error = std::optional<std::string>{};
  try {
    store(parse_command_line(argc, argv, all), vm);
    notify(vm);
  } catch (po::error const& ex) { error = ex.what(); }

  return {all, vm, error};
}
