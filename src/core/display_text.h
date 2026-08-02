#pragma once

#include <indicators/display_width.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace displaytext {

inline auto displayWidth(std::string_view text) -> std::size_t {
  auto const width = unicode::display_width(std::string{text});
  if (width < 0) { return text.size(); }
  return static_cast<std::size_t>(width);
}

inline auto utf8CodePointLength(std::string_view text, std::size_t offset)
  -> std::size_t {
  if (offset >= text.size()) { return 0; }

  auto const lead = static_cast<unsigned char>(text[offset]);
  auto length = std::size_t{1};

  if ((lead & 0x80u) == 0) {
    length = 1;
  } else if ((lead & 0xE0u) == 0xC0u) {
    length = 2;
  } else if ((lead & 0xF0u) == 0xE0u) {
    length = 3;
  } else if ((lead & 0xF8u) == 0xF0u) {
    length = 4;
  } else {
    return 1;
  }

  if (offset + length > text.size()) { return 1; }

  for (auto index = std::size_t{1}; index < length; ++index) {
    auto const ch = static_cast<unsigned char>(text[offset + index]);
    if ((ch & 0xC0u) != 0x80u) { return 1; }
  }

  return length;
}

inline auto takePrefixByDisplayWidth(std::string_view text, std::size_t maxWidth)
  -> std::string {
  if (maxWidth == 0 || text.empty()) { return {}; }

  auto out = std::string{};
  out.reserve(text.size());
  auto width = std::size_t{0};
  auto index = std::size_t{0};

  while (index < text.size()) {
    auto const length = utf8CodePointLength(text, index);
    auto const next = std::string{text.substr(index, length)};
    auto const nextWidth = std::max<std::size_t>(1, displayWidth(next));
    if (width + nextWidth > maxWidth) { break; }
    out += next;
    width += nextWidth;
    index += length;
  }

  return out;
}

inline auto takeWindowByDisplayWidth(
  std::string_view text,
  std::size_t startCol,
  std::size_t maxWidth
) -> std::string {
  if (maxWidth == 0 || text.empty()) { return {}; }

  auto index = std::size_t{0};
  auto col = std::size_t{0};
  while (index < text.size()) {
    auto const length = utf8CodePointLength(text, index);
    auto const nextWidth =
      std::max<std::size_t>(1, displayWidth(text.substr(index, length)));
    if (col + nextWidth > startCol) { break; }
    col += nextWidth;
    index += length;
  }

  auto out = std::string{};
  out.reserve(text.size() - index);
  auto width = std::size_t{0};
  while (index < text.size()) {
    auto const length = utf8CodePointLength(text, index);
    auto const next = std::string{text.substr(index, length)};
    auto const nextWidth = std::max<std::size_t>(1, displayWidth(next));
    if (width + nextWidth > maxWidth) { break; }
    out += next;
    width += nextWidth;
    index += length;
  }

  return out;
}

inline auto truncateWithEllipsis(std::string_view text, std::size_t maxWidth)
  -> std::string {
  if (maxWidth == 0) { return {}; }
  if (displayWidth(text) <= maxWidth) { return std::string{text}; }
  if (maxWidth <= 3) { return takePrefixByDisplayWidth(text, maxWidth); }
  return takePrefixByDisplayWidth(text, maxWidth - 3) + "...";
}

inline auto pathToUtf8String(std::filesystem::path const& path) -> std::string {
  auto const utf8 = path.u8string();
  auto out = std::string{};
  out.reserve(utf8.size());
  for (auto const ch: utf8) { out.push_back(static_cast<char>(ch)); }
  return out;
}

}  // namespace displaytext
