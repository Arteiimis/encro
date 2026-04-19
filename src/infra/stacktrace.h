#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace crash {

auto captureStacktrace(std::size_t skipFrames = 0, std::size_t maxFrames = 64)
  -> std::vector<std::string>;

auto formatStacktrace(std::vector<std::string> const& frames) -> std::string;

}  // namespace crash
