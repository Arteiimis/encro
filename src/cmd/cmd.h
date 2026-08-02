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
  std::optional<std::string> nvencPreset;

  // ── File operation options ─────────────────────────────────────
  bool pack = false;
  bool packOnly = false;
  bool overwrite = false;

  // ── Help output (rendered by formatter_fn) ─────────────────────
  std::string helpText;

  // ── Parse error (CLI11-native message) ─────────────────────────
  std::optional<std::string> error;
};

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult;
