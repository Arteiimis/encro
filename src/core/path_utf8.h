#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Paths cross this program as UTF-8 strings: command-line arguments, config
// values and the strings stored in job-state.json are all UTF-8.
//
// `std::filesystem::path` has no portable UTF-8 constructor — `path{std::string}`
// converts with the process locale's codecvt, which on Windows is the ANSI code
// page, not UTF-8. A Chinese or Japanese path therefore survives on a machine
// whose ANSI code page happens to be CJK and turns into mojibake on any other,
// which is exactly how the first Windows CI runs failed.
//
// Going through `std::u8string` states the encoding instead of inheriting it.
namespace pathutf8 {

inline auto toPath(std::string_view utf8) -> std::filesystem::path {
  return std::filesystem::path{
    std::u8string{reinterpret_cast<char8_t const*>(utf8.data()), utf8.size()}
  };
}

// The inverse, for anything written back out as text (JSON, log lines).
inline auto fromPath(std::filesystem::path const& path) -> std::string {
  auto const utf8 = path.u8string();
  return std::string{reinterpret_cast<char const*>(utf8.data()), utf8.size()};
}

}  // namespace pathutf8
