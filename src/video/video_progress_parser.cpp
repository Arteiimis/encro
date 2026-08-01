#include "video/video_progress_parser.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <boost/parser/parser.hpp>

#include <cctype>
#include <deque>
#include <fstream>

namespace fs = std::filesystem;

DEFINE_LOGGER(logtags::VIDEO_PROGRESS);

namespace {

constexpr auto kProgressTailLines = std::size_t{32};

auto containsCaseInsensitive(std::string_view text, std::string_view needle) -> bool {
  if (needle.empty()) { return true; }
  if (text.size() < needle.size()) { return false; }

  for (std::size_t i = 0; i + needle.size() <= text.size(); ++i) {
    auto match = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      auto const tc = static_cast<unsigned char>(text[i + j]);
      auto const nc = static_cast<unsigned char>(needle[j]);
      if (std::tolower(tc) != std::tolower(nc)) {
        match = false;
        break;
      }
    }
    if (match) { return true; }
  }

  return false;
}

auto trimWhitespace(std::string_view text) -> std::string_view {
  auto const begin = text.find_first_not_of(" \t\r");
  if (begin == std::string_view::npos) { return {}; }
  auto const end = text.find_last_not_of(" \t\r");
  return text.substr(begin, end - begin + 1);
}

auto startsWithCaseInsensitive(std::string_view text, std::string_view prefix) -> bool {
  if (text.size() < prefix.size()) { return false; }

  for (std::size_t i = 0; i < prefix.size(); ++i) {
    auto const tc = static_cast<unsigned char>(text[i]);
    auto const pc = static_cast<unsigned char>(prefix[i]);
    if (std::tolower(tc) != std::tolower(pc)) { return false; }
  }

  return true;
}

auto isLikelyFfmpegMetadataLine(std::string_view line) -> bool {
  auto const trimmed = trimWhitespace(line);
  if (trimmed.empty()) { return false; }

  return startsWithCaseInsensitive(trimmed, "metadata:")
    || startsWithCaseInsensitive(trimmed, "comment")
    || startsWithCaseInsensitive(trimmed, "major_brand")
    || startsWithCaseInsensitive(trimmed, "minor_version")
    || startsWithCaseInsensitive(trimmed, "compatible_brands")
    || startsWithCaseInsensitive(trimmed, "encoder")
    || startsWithCaseInsensitive(trimmed, "handler_name")
    || startsWithCaseInsensitive(trimmed, "input #")
    || startsWithCaseInsensitive(trimmed, "output #")
    || startsWithCaseInsensitive(trimmed, "stream #")
    || startsWithCaseInsensitive(trimmed, "stream mapping:")
    || startsWithCaseInsensitive(trimmed, "duration:")
    || startsWithCaseInsensitive(trimmed, "press [q] to stop");
}

}  // namespace

auto isLikelyFfmpegErrorLine(std::string_view line) -> bool {
  auto const trimmed = trimWhitespace(line);
  if (trimmed.empty() || isLikelyFfmpegMetadataLine(trimmed)) { return false; }

  if (
    startsWithCaseInsensitive(trimmed, "error")
    || startsWithCaseInsensitive(trimmed, "failed")
    || startsWithCaseInsensitive(trimmed, "invalid")
    || startsWithCaseInsensitive(trimmed, "could not")
    || startsWithCaseInsensitive(trimmed, "unable to")
    || startsWithCaseInsensitive(trimmed, "conversion failed")
  ) {
    return true;
  }

  if (
    containsCaseInsensitive(trimmed, "no such file or directory")
    || containsCaseInsensitive(trimmed, "permission denied")
    || containsCaseInsensitive(trimmed, "matches no streams")
    || containsCaseInsensitive(trimmed, "not found")
  ) {
    return true;
  }

  auto const bracketedDiagnostic =
    trimmed.starts_with('[') && trimmed.find(']') != std::string_view::npos;
  if (!bracketedDiagnostic) { return false; }

  return containsCaseInsensitive(trimmed, "] error")
    || containsCaseInsensitive(trimmed, "] failed")
    || containsCaseInsensitive(trimmed, "] invalid")
    || containsCaseInsensitive(trimmed, "] could not")
    || containsCaseInsensitive(trimmed, "] unable to")
    || containsCaseInsensitive(trimmed, "] not found");
}

auto readLastNLines(fs::path const& filePath, std::size_t n) -> std::vector<std::string> {
  auto file = std::ifstream{filePath};
  if (!file.is_open() || n == 0) { return {}; }

  auto tail = std::deque<std::string>{};
  auto line = std::string{};

  while (std::getline(file, line)) {
    if (tail.size() == n) { tail.pop_front(); }
    tail.push_back(line);
  }

  return {tail.begin(), tail.end()};
}

auto parseProgressFile(fs::path const& progressFilePath) -> std::optional<ProgressData> {
  namespace bp = boost::parser;

  auto const lines = readLastNLines(progressFilePath, kProgressTailLines);
  if (lines.empty()) { return std::nullopt; }

  auto frameCount = std::optional<uint64_t>{};
  auto progressStatus = std::string{};

  auto const frameParser = bp::string("frame=") >> bp::uint_;
  auto const progressParser = bp::string("progress=") >> *bp::char_;

  for (auto const& line: lines) {
    if (auto const& res = parse(line, frameParser); res.has_value()) {
      auto [_, parsedFrameCount] = res.value();
      frameCount = parsedFrameCount;
    }
    if (auto const& res = parse(line, progressParser); res.has_value()) {
      auto [_, parsedStatus] = res.value();
      progressStatus = parsedStatus;
    }
  }

  if (!frameCount.has_value()) { return std::nullopt; }

  return ProgressData{frameCount.value(), progressStatus};
}

auto parseSegmentEndUs(fs::path const& progressFilePath) -> std::optional<std::uint64_t> {
  namespace bp = boost::parser;

  auto const lines = readLastNLines(progressFilePath, kProgressTailLines);
  if (lines.empty()) { return std::nullopt; }

  auto endUs = std::optional<std::uint64_t>{};

  auto const endUsParser = bp::string("out_time_us=") >> bp::uint_;

  for (auto const& line: lines) {
    if (auto const& res = parse(line, endUsParser); res.has_value()) {
      endUs = std::get<1>(res.value());
    }
  }

  return endUs;
}
