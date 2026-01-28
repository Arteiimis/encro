#pragma once

#include <filesystem>
#include <optional>

#include <boost/process/v1.hpp>
#include <boost/json.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/static_string.hpp>
#include <indicators/progress_bar.hpp>

#include "error_handle.h"

namespace fs = std::filesystem;

auto exec2(std::string_view cmd) -> std::pair<int, std::string>;

bool readUserIpt();

auto findFFprobe() -> std::optional<fs::path>;

auto findFFmpeg() -> std::optional<fs::path>;

auto find7zip() -> std::optional<fs::path>;

auto toolCheck() -> eh::Result<void>;

std::string getUUID();

auto getProgressBar(std::string_view promptText)
  -> std::unique_ptr<indicators::ProgressBar>;

void cursorToggleVisibility(bool visible);

auto getTrimedPath(const std::string& pathStr) -> fs::path;
