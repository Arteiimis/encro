#pragma once

#include <CLI/CLI.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct CmdParseResult {
  // ── General options ────────────────────────────────────────────
  bool help = false;
  bool version = false;
  bool verbose = false;
  bool fullProgress = false;
  bool jsonEnabled = false;
  std::string color = "auto";
  bool yesToAll = false;

  // ── Input/Output options ───────────────────────────────────────
  std::optional<std::string> input;
  std::optional<std::vector<std::string>> inputs;
  std::optional<std::vector<std::string>> positionalInputs;
  std::optional<std::string> output;
  std::optional<std::string> stateFile;
  std::string outputFormat = "mp4";
  bool keep = false;
  std::string forceConflictHandling = "y";
  bool folderSummary = false;
  bool recursive = false;

  // ── Processing options ─────────────────────────────────────────
  std::string processType = "video";
  std::optional<std::size_t> maxJobs;
  bool resume = false;
  bool restart = false;
  std::optional<std::string> ffmpegPath;
  bool compress = false;
  std::optional<int> imageQuality;
  std::optional<int> crf;
  int minVmaf = 95;
  bool dryRun = false;
  std::optional<std::string> nvencPreset;
  std::optional<std::string> videoCodec;

  // ── Preview subcommand ─────────────────────────────────────────
  bool preview = false;
  std::optional<std::string> previewOriginal;
  std::optional<std::string> previewEncoded;
  std::optional<std::string> previewOutput;
  std::optional<double> previewStart;
  std::optional<double> previewDuration;
  bool previewNoOpen = false;

  // ── File operation options ─────────────────────────────────────
  bool pack = false;
  bool packOnly = false;
  bool overwrite = false;

  // ── config subcommand (mutually exclusive actions) ──────
  bool config = false;
  bool configList = false;
  std::optional<std::string> configGet;
  std::optional<std::vector<std::string>> configSet;
  std::optional<std::string> configUnset;
  bool configPath = false;

  // ── Help output (rendered by formatter_fn) ──────────────────────
  std::string helpText;

  // ── Parse error (CLI11-native message) ─────────────────────────
  std::optional<std::string> error;
};

// Registered CLI tree shared by the parse path and the completion emitter.
// The app is intentionally leaked: the config-key registry keeps option
// pointers for the process lifetime (see buildAppTree).
struct AppTree {
  CLI::App* app = nullptr;
  CLI::App* previewSub = nullptr;
  CLI::App* configSub = nullptr;
};

// Registers the whole command surface onto a fresh CLI11 app. With
// injectConfig set, stored user config is applied as forced option defaults;
// the completion emitter passes false so emission never reads the stored
// config (add-shell-completion design D1).
auto buildAppTree(CmdParseResult& result, std::string const& introLine, bool injectConfig)
  -> AppTree;

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult;
