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
  int verbosity = 0;  // 0 off, 1 curated echo (-v), 2 full debug (-vv/--debug)
  bool quiet = false;
  bool debug = false;
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

  // ── organize subcommand ────────────────────────────────────────
  bool organize = false;
  std::optional<std::string> organizeDir;
  std::optional<double> organizeMinConfidence;
  std::optional<std::string> organizeModelDir;
  bool organizeDownloadModels = false;
  bool organizeRecluster = false;

  // ── File operation options ─────────────────────────────────────
  bool pack = false;
  bool packOnly = false;
  bool overwrite = false;

  // ── config subcommand (positional action verbs) ──────
  bool config = false;
  std::string configVerb;  // list|get|set|unset|path; empty when absent
  std::optional<std::string> configKey;
  std::optional<std::string> configValue;

  // ── completion subcommand (install/uninstall mutually exclusive) ──
  bool completion = false;
  std::string completionShell;  // "powershell" or "bash"; empty when absent
  bool completionInstall = false;
  bool completionUninstall = false;

  // ── Help output (rendered by formatter_fn) ──────────────────────
  // Help is rendered lazily: the formatter queries the terminal color mode
  // at render time, so the string is built when it is read (after the mode
  // is configured), not during parse. helpApp_ points at the app whose help
  // to render; it outlives the result because the app tree is intentionally
  // leaked (see AppTree). buildAndParse installs it on every return path.
  CLI::App* helpApp_ = nullptr;

  auto helpText() const -> std::string { return helpApp_->help(); }

  // ── Parse error (CLI11-native message) ─────────────────────────
  std::optional<std::string> error;
};

// Registered CLI tree shared by the parse path and the completion emitter.
// The app is intentionally leaked: the config-key registry keeps option
// pointers for the process lifetime (see buildAppTree).
struct AppTree {
  CLI::App* app = nullptr;
  CLI::App* previewSub = nullptr;
  CLI::App* organizeSub = nullptr;
  CLI::App* configSub = nullptr;
  CLI::App* completionSub = nullptr;
};

// Registers the whole command surface onto a fresh CLI11 app. With
// injectConfig set, stored user config is applied as forced option defaults;
// the completion emitter passes false so emission never reads the stored
// config (add-shell-completion design D1).
auto buildAppTree(CmdParseResult& result, std::string const& introLine, bool injectConfig)
  -> AppTree;

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult;
