#pragma once

#include "cmd/config_store.h"
#include "core/app_context.h"

#include <CLI/CLI.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct CmdParseResult {
  // Which stored template the default layer applied and where it came from:
  // always set together, so they travel as one value.
  struct TemplateAttribution {
    std::string name;
    std::string path;  // UTF-8
  };
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
  bool videoWebp = false;
  std::optional<int> imageQuality;
  std::optional<int> crf;
  int minVmaf = appctx::kDefaultMinVmaf;
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
  std::optional<double> organizeIdentityTau;
  std::optional<std::vector<std::string>> organizeIngest;
  std::optional<std::vector<std::string>> organizeIgnoreFolder;
  bool organizeDownloadModels = false;
  bool organizeRecluster = false;

  // ── batch subcommand ───────────────────────────────────────────
  bool batch = false;
  std::optional<std::string> batchRoot;    // positional root directory
  std::optional<std::string> batchOutput;  // -o packed-root override (plain path)
  std::optional<std::vector<std::string>> batchInputsStub;  // rejected -I form

  // ── File operation options ─────────────────────────────────────
  bool pack = false;
  bool packOnly = false;

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

  // ── template subcommand (positional action verbs) ──────
  bool templates = false;
  std::string templateVerb;                // list|show|save|unset|delete|path|schema
  std::optional<std::string> templateName;
  std::optional<std::string> templateKey;  // unset only

  // ── selected template (-T) ──────
  std::optional<std::string> selectedTemplate;  // name given with -T
  bool templateListRequested = false;           // -T with no name
  // Values `template save` would store, collected from the probe parse (never
  // from config-injected defaults), and the attribution of the template the
  // default layer applied (for the -v line).
  std::map<std::string, std::string> capturedTemplateValues;
  std::optional<TemplateAttribution> appliedTemplate;

  // ── Config-key table (design D4/D5) ────────────────────────────
  // keyEntries holds the registration-time drafts (registration order),
  // consumed once by the assemble step in buildAppTree; every registrar
  // already receives this object, so no extra plumbing. keyTable is the
  // assembled table every config reader goes through.
  std::vector<configstore::KeyDef> keyEntries;
  configstore::KeyTable keyTable;
  std::vector<configstore::KeyDef> templateEntries;
  configstore::KeyTable templateTable;

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
// The app is intentionally leaked: the completion registry keeps positional
// option pointers for the process lifetime (see buildAppTree).
struct AppTree {
  CLI::App* app = nullptr;
  CLI::App* previewSub = nullptr;
  CLI::App* organizeSub = nullptr;
  CLI::App* batchSub = nullptr;
  CLI::App* configSub = nullptr;
  CLI::App* completionSub = nullptr;
  CLI::App* templateSub = nullptr;
};

// Registers the whole command surface onto a fresh CLI11 app. With
// injectConfig set, stored user config is applied as forced option defaults;
// the completion emitter passes false so emission never reads the stored
// config (add-shell-completion design D1). `selectedTemplate` is the name the
// probe parse read from -T: injection happens before the parse that would
// populate the binding itself, so the name has to arrive as an argument
// (command-templates design D3).
auto buildAppTree(
  CmdParseResult& result,
  std::string const& introLine,
  bool injectConfig,
  std::string const& selectedTemplate = {}
) -> AppTree;

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult;
