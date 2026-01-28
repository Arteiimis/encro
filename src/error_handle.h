#pragma once

#include <string>
#include <expected>
#include <format>

namespace ErrorHandle {

template<class Ty> using Result = std::expected<Ty, std::string>;

template<class... Tys>
auto makeError(const std::format_string<Tys...> fmt, Tys&&... args) {
  return std::unexpected(std::format(fmt, std::forward<Tys>(args)...));
}

}  // namespace ErrorHandle

namespace eh = ErrorHandle;
