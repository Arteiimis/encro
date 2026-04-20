#pragma once

#include <cstddef>
#include <optional>

namespace consolewidth {

struct Config {
  std::size_t defaultColumns = 80;
  std::size_t minColumns = 1;
  std::optional<std::size_t> maxColumns;
};

auto resolveColumns(Config const& config = {}) -> std::size_t;

}  // namespace consolewidth
