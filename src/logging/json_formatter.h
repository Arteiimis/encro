#pragma once

#include <spdlog/formatter.h>
#include <spdlog/spdlog.h>

#include <boost/json.hpp>

#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace logging {

class JsonFormatter final : public spdlog::formatter {
public:
  auto format(spdlog::details::log_msg const& msg,
              spdlog::memory_buf_t& dest) -> void override;
  auto clone() const -> std::unique_ptr<spdlog::formatter> override;

private:
  static auto formatTimestamp(spdlog::log_clock::time_point tp) -> std::string;
  static auto extractErrorContext(std::string_view payload)
    -> std::vector<std::string>;
  static auto extractElapsedMs(std::string_view payload)
    -> std::optional<int64_t>;
};

// ── Implementation ──────────────────────────────────────────────────────────

inline auto JsonFormatter::format(spdlog::details::log_msg const& msg,
                                   spdlog::memory_buf_t& dest) -> void {
  namespace json = boost::json;

  auto obj = json::object{};

  // Fixed fields (always present per D-10)
  obj["timestamp"] = formatTimestamp(msg.time);
  auto const levelSv = spdlog::level::to_string_view(msg.level);
  obj["level"] = json::string{levelSv.data(), levelSv.size()};
  obj["module"] =
    json::string{msg.logger_name.data(), msg.logger_name.size()};

  // Source: "filename:line" from structured source_loc
  if (msg.source.empty()) {
    obj["source"] = json::string{};
  } else {
    obj["source"] = json::string{std::format(
      "{}:{}",
      msg.source.filename != nullptr ? msg.source.filename : "",
      msg.source.line)};
  }

  // Payload processing: extract optional fields, strip context suffix
  auto const payload =
    std::string_view{msg.payload.data(), msg.payload.size()};

  auto message = std::string{payload};
  auto ctxFrames = extractErrorContext(payload);

  if (!ctxFrames.empty()) {
    // Strip context suffix: everything before " [context:"
    auto const suffixPos = payload.rfind(" [context:");
    if (suffixPos != std::string_view::npos) {
      message = std::string{payload.substr(0, suffixPos)};
    }
    // Build error_context JSON array
    auto arr = json::array{};
    for (auto const& frame : ctxFrames) {
      arr.push_back(json::string{frame});
    }
    obj["error_context"] = std::move(arr);
  }

  obj["message"] = std::move(message);

  // Optional: elapsed_ms from ScopedTimer completion pattern
  if (auto const elapsed = extractElapsedMs(payload)) {
    obj["elapsed_ms"] = *elapsed;
  }

  // Serialize to single-line JSON + NDJSON newline
  auto const line = json::serialize(obj);
  dest.append(line.data(), line.data() + line.size());
  dest.push_back('\n');
}

inline auto JsonFormatter::clone() const
  -> std::unique_ptr<spdlog::formatter> {
  return std::make_unique<JsonFormatter>();
}

// ── Private helpers ─────────────────────────────────────────────────────────

inline auto JsonFormatter::formatTimestamp(
  spdlog::log_clock::time_point tp) -> std::string {
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", tp);
}

inline auto JsonFormatter::extractErrorContext(std::string_view payload)
  -> std::vector<std::string> {
  // Context suffix is always " [context: ...]" at end of message (D-11)
  auto const markerPos = payload.rfind(" [context:");
  if (markerPos == std::string_view::npos) {
    return {};
  }

  // Content between " [context: " and trailing "]"
  auto const contentStart = markerPos + 11;  // strlen(" [context: ") == 11
  auto const contentEnd = payload.size();
  if (contentStart >= contentEnd) {
    return {};
  }

  // Trim trailing "]"
  auto ctxContent = payload.substr(contentStart, contentEnd - contentStart);
  if (ctxContent.ends_with("]")) {
    ctxContent.remove_suffix(1);
  }

  // Split by " > " into individual context frames
  auto frames = std::vector<std::string>{};
  auto pos = std::size_t{0};
  while (pos < ctxContent.size()) {
    auto const sep = ctxContent.find(" > ", pos);
    auto const frame = (sep == std::string_view::npos)
                         ? ctxContent.substr(pos)
                         : ctxContent.substr(pos, sep - pos);
    if (!frame.empty()) {
      frames.emplace_back(frame);
    }
    if (sep == std::string_view::npos) {
      break;
    }
    pos = sep + 3;  // strlen(" > ") == 3
  }

  return frames;
}

inline auto JsonFormatter::extractElapsedMs(std::string_view payload)
  -> std::optional<int64_t> {
  // ScopedTimer format: "completed in Xms" where X is an integer
  auto const markerPos = payload.find("completed in ");
  if (markerPos == std::string_view::npos) {
    return std::nullopt;
  }

  auto const numStart =
    markerPos + 13;  // strlen("completed in ") == 13
  auto const numEnd = payload.find("ms", numStart);
  if (numEnd == std::string_view::npos || numEnd <= numStart) {
    return std::nullopt;
  }

  auto const numStr = payload.substr(numStart, numEnd - numStart);
  auto value = int64_t{0};
  for (auto const ch : numStr) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    value = value * 10 + (ch - '0');
  }

  return value;
}

}  // namespace logging
