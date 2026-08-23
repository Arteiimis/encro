#pragma once

#include "logging/setup.h"

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

class JsonFormatter final: public spdlog::formatter {
public:
  void format(spdlog::details::log_msg const& msg, spdlog::memory_buf_t& dest) override;
  auto clone() const -> std::unique_ptr<spdlog::formatter> override;

private:
  static auto formatTimestamp(spdlog::log_clock::time_point tp) -> std::string;
  static auto extractErrorContext(std::string_view payload) -> std::vector<std::string>;
  static auto extractElapsedMs(std::string_view payload) -> std::optional<int64_t>;
  static auto extractAttributes(std::string_view payload)
    -> std::pair<std::optional<std::size_t>, std::optional<boost::json::object>>;
  static auto parseRunSummary(std::string_view payload)
    -> std::optional<boost::json::object>;
};

// ── Implementation ──────────────────────────────────────────────────────────

inline void
JsonFormatter::format(spdlog::details::log_msg const& msg, spdlog::memory_buf_t& dest) {
  namespace json = boost::json;

  auto obj = json::object{};

  // Fixed fields (always present per D-10)
  obj["timestamp"] = formatTimestamp(msg.time);
  auto const levelSv = spdlog::level::to_string_view(msg.level);
  obj["level"] = json::string{levelSv.data(), levelSv.size()};
  obj["module"] = json::string{msg.logger_name.data(), msg.logger_name.size()};

  // Source: "filename:line" from structured source_loc
  if (msg.source.empty()) {
    obj["source"] = json::string{};
  } else {
    obj["source"] = json::string{std::format(
      "{}:{}",
      msg.source.filename != nullptr ? msg.source.filename : "",
      msg.source.line
    )};
  }

  // Payload processing: extract optional fields, strip context suffix
  auto const payload = std::string_view{msg.payload.data(), msg.payload.size()};

  // Attrs suffix is appended last by the LOG_* macros; parse it before context
  // so context extraction stops at the attrs marker.
  auto const [attrsText, attrs] = extractAttributes(payload);

  auto message = std::string{payload};
  auto ctxFrames = extractErrorContext(payload);

  if (attrsText.has_value()) {
    // Strip attrs suffix from the message
    message = std::string{payload.substr(0, attrsText.value())};
  }

  if (!ctxFrames.empty()) {
    // Strip context suffix: everything before " [context:"
    auto const suffixPos = payload.rfind(" [context:");
    if (suffixPos != std::string_view::npos) {
      message = std::string{payload.substr(0, suffixPos)};
    }
    // Build error_context JSON array
    auto arr = json::array{};
    for (auto const& frame: ctxFrames) { arr.push_back(json::string{frame}); }
    obj["error_context"] = arr;
  }

  obj["message"] = message;

  // Correlation fields from the attribute chain (never override fixed fields)
  if (attrs.has_value()) {
    for (auto const& [key, value]: attrs.value()) {
      if (!obj.contains(key)) { obj[key] = value; }
    }
  }

  // run_id: stable per run, injected for every record
  obj["run_id"] = runId();

  // End-of-run summary: turn the RUN SUMMARY: key=value body into a summary
  // object (log path + level_counts attached here, not in the body string)
  if (auto summary = parseRunSummary(payload); summary.has_value()) {
    obj["summary"] = std::move(summary.value());
  }

  // Optional: elapsed_ms from ScopedTimer completion pattern
  if (auto const elapsed = extractElapsedMs(payload)) { obj["elapsed_ms"] = *elapsed; }

  // Serialize to single-line JSON + NDJSON newline
  auto const line = json::serialize(obj);
  dest.append(line.data(), line.data() + line.size());
  dest.push_back('\n');
}

inline auto JsonFormatter::clone() const -> std::unique_ptr<spdlog::formatter> {
  return std::make_unique<JsonFormatter>();
}

inline auto JsonFormatter::parseRunSummary(std::string_view payload)
  -> std::optional<boost::json::object> {
  namespace json = boost::json;

  if (!payload.starts_with("RUN SUMMARY: ")) { return std::nullopt; }

  auto summary = json::object{};
  auto summaryEc = boost::system::error_code{};
  auto rest = payload.substr(std::string_view{"RUN SUMMARY: "}.size());
  while (!rest.empty()) {
    auto const space = rest.find(' ');
    auto const token = (space == std::string_view::npos) ? rest : rest.substr(0, space);
    auto const eq = token.find('=');
    if (eq != std::string_view::npos) {
      auto const key = token.substr(0, eq);
      auto const value = token.substr(eq + 1);
      // log/level_counts are regenerated authoritatively below
      if (key == "log" || key == "level_counts") { /* skip */
      } else if (key == "tasks_total" || key == "tasks_failed" || key == "elapsed_ms") {
        auto const parsed = json::parse(value, summaryEc);
        summary[key] = parsed;
      } else {
        summary[key] = json::string{value};
      }
    }
    if (space == std::string_view::npos) { break; }
    rest.remove_prefix(space + 1);
  }
  if (auto const logPath = currentLogFilePath(); logPath.has_value()) {
    summary["log"] = json::string{logPath->string()};
  }
  auto levelCountsObj = json::object{};
  for (auto const& [level, count]: levelCounts()) { levelCountsObj[level] = count; }
  summary["level_counts"] = std::move(levelCountsObj);
  return summary;
}

// ── Private helpers ─────────────────────────────────────────────────────────

inline auto JsonFormatter::formatTimestamp(spdlog::log_clock::time_point tp)
  -> std::string {
  // RFC 3339 UTC with millisecond precision: YYYY-MM-DDTHH:MM:SS.sssZ
  // (matches the human pattern's %e precision)
  auto const millis =
    std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
  // Second precision so %S does not re-emit the fraction
  auto const wholeSeconds =
    std::chrono::sys_seconds{std::chrono::seconds{millis.count() / 1000}};
  auto const ms = millis.count() % 1000;
  return std::format("{:%Y-%m-%dT%H:%M:%S}.{:03d}Z", wholeSeconds, ms);
}

inline auto JsonFormatter::extractErrorContext(std::string_view payload)
  -> std::vector<std::string> {
  // Context suffix is always " [context: ...]" at end of message (D-11)
  auto const markerPos = payload.rfind(" [context:");
  if (markerPos == std::string_view::npos) { return {}; }

  // Content between " [context: " and the attrs marker (or trailing "]")
  auto const contentStart = markerPos + 11;  // strlen(" [context: ") == 11
  auto contentEnd = payload.size();
  if (
    auto const attrsPos = payload.rfind(" [attrs: ");
    attrsPos != std::string_view::npos && attrsPos > markerPos
  ) {
    contentEnd = attrsPos;
  }
  if (contentStart >= contentEnd) { return {}; }

  // Trim trailing "]"
  auto ctxContent = payload.substr(contentStart, contentEnd - contentStart);
  if (ctxContent.ends_with("]")) { ctxContent.remove_suffix(1); }

  // Split by " > " into individual context frames
  auto frames = std::vector<std::string>{};
  auto pos = std::size_t{0};
  while (pos < ctxContent.size()) {
    auto const sep = ctxContent.find(" > ", pos);
    auto const frame = (sep == std::string_view::npos)
      ? ctxContent.substr(pos)
      : ctxContent.substr(pos, sep - pos);
    if (!frame.empty()) { frames.emplace_back(frame); }
    if (sep == std::string_view::npos) { break; }
    pos = sep + 3;  // strlen(" > ") == 3
  }

  return frames;
}

inline auto JsonFormatter::extractElapsedMs(std::string_view payload)
  -> std::optional<int64_t> {
  // ScopedTimer format: "completed in Xms" where X is an integer
  auto const markerPos = payload.find("completed in ");
  if (markerPos == std::string_view::npos) { return std::nullopt; }

  auto const numStart = markerPos + 13;  // strlen("completed in ") == 13
  auto const numEnd = payload.find("ms", numStart);
  if (numEnd == std::string_view::npos || numEnd <= numStart) { return std::nullopt; }

  auto const numStr = payload.substr(numStart, numEnd - numStart);
  auto value = int64_t{0};
  for (auto const ch: numStr) {
    if (ch < '0' || ch > '9') { return std::nullopt; }
    value = value * 10 + (ch - '0');
  }

  return value;
}

inline auto JsonFormatter::extractAttributes(std::string_view payload)
  -> std::pair<std::optional<std::size_t>, std::optional<boost::json::object>> {
  namespace json = boost::json;

  // Attrs suffix is appended last: " [attrs: {json-object}]"
  auto const markerPos = payload.rfind(" [attrs: ");
  if (markerPos == std::string_view::npos) { return {std::nullopt, std::nullopt}; }

  // The suffix structure guarantees the final character is "]" (the object's
  // own closing brace belongs to the JSON). Strip it and parse the object.
  auto attrsText = payload.substr(markerPos + 9);  // strlen(" [attrs: ") == 9
  if (!attrsText.ends_with("]")) { return {std::nullopt, std::nullopt}; }
  attrsText.remove_suffix(1);

  auto ec = boost::system::error_code{};
  auto parsed = json::parse(attrsText, ec);
  if (parsed.is_object()) { return {markerPos, parsed.as_object()}; }
  return {std::nullopt, std::nullopt};
}

}  // namespace logging
