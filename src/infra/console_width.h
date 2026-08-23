#pragma once

#include <cstddef>
#include <optional>

namespace consolewidth {

struct Config {
  std::size_t defaultColumns = 80;
  std::size_t minColumns = 1;
  std::optional<std::size_t> maxColumns;
};

std::size_t resolveColumns(Config const& config = {});

}  // namespace consolewidth
