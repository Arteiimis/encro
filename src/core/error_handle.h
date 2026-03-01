#pragma once

#include <expected>
#include <format>
#include <string>

namespace ErrorHandle {

template<class Ty> using Result = std::expected<Ty, std::string>;

template<class... Tys>
auto makeError(std::format_string<Tys...> const fmt, Tys&&... args) {
  return std::unexpected(std::format(fmt, std::forward<Tys>(args)...));
}

}  // namespace ErrorHandle

namespace eh = ErrorHandle;
