#pragma once

#include <filesystem>
#include <optional>

#include <boost/process/v1.hpp>
#include <boost/json.hpp>

namespace fs = std::filesystem;

auto exec2(std::string_view cmd) -> std::pair<int, std::string>;

bool readUserIpt();

auto findFFprobe() -> std::optional<fs::path>;

auto findFFmpeg() -> std::optional<fs::path>;

bool toolCheck();

std::string getUUID();
