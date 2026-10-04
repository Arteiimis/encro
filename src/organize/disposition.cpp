#include "organize/disposition.h"

#include "core/display_text.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <set>
#include <system_error>

namespace organize {

namespace {

// Trimmed ASCII whitespace + ASCII lowercasing, byte-wise: the list's folding
// rule. Non-ASCII bytes pass through untouched, so CJK names compare exactly
// and are never folded or converted.
auto foldedTrimmed(std::string const& name) -> std::string {
  constexpr auto kWhitespace = std::string_view{" \t\n\r\f\v"};
  auto const view = std::string_view{name};
  auto const begin = view.find_first_not_of(kWhitespace);
  if (begin == std::string_view::npos) { return {}; }
  auto const end = view.find_last_not_of(kWhitespace);
  auto out = std::string{view.substr(begin, end - begin + 1)};
  for (auto& c: out) {
    if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
  }
  return out;
}

}  // namespace

bool isMiscFolderName(std::string const& name) {
  auto const folded = foldedTrimmed(name);
  if (folded.empty()) { return false; }
  return std::ranges::find(kMiscFolderNames, folded) != kMiscFolderNames.end();
}

auto computeFolderDispositions(
  fs::path const& root,
  bool recursive,
  std::vector<std::string> const& ingestNames,
  std::vector<std::string> const& ignoreNames
) -> std::vector<FolderDisposition> {
  auto ec = std::error_code{};
  if (!fs::exists(root, ec) || ec) { return {}; }

  auto dispositions = std::vector<FolderDisposition>{};
  for (auto const& entry: fs::directory_iterator{root, ec}) {
    if (ec || !entry.is_directory()) { continue; }
    auto const name = displaytext::pathToUtf8String(entry.path().filename());
    if (name == "organized") { continue; }

    auto kind = DispositionKind::Reference;
    auto reason = DispositionReason::Default;
    if (std::ranges::find(ignoreNames, name) != ignoreNames.end()) {
      kind = DispositionKind::Ignored;
      reason = DispositionReason::Flag;
    } else if (recursive) {
      // Whole-set behavior: everything not ignored is input, dot folders
      // included.
      kind = DispositionKind::Input;
    } else if (std::ranges::find(ingestNames, name) != ingestNames.end()) {
      kind = DispositionKind::Input;
      reason = DispositionReason::Flag;
    } else if (name.starts_with('.')) {
      kind = DispositionKind::Ignored;
    } else if (isMiscFolderName(name)) {
      kind = DispositionKind::Input;
      reason = DispositionReason::NameList;
    }
    dispositions.push_back(
      FolderDisposition{.name = entry.path().filename(), .kind = kind, .reason = reason}
    );
  }

  // Folder-name order, like every list the run compares names across; the
  // directory walk's order is unspecified.
  std::sort(
    dispositions.begin(),
    dispositions.end(),
    [](FolderDisposition const& a, FolderDisposition const& b) {
      return displaytext::pathToUtf8String(a.name)
        < displaytext::pathToUtf8String(b.name);
    }
  );
  return dispositions;
}

auto validateDispositionFlags(
  fs::path const& root,
  std::vector<std::string> const& ingestNames,
  std::vector<std::string> const& ignoreNames
) -> std::optional<std::string> {
  for (auto const& name: ingestNames) {
    if (std::ranges::find(ignoreNames, name) != ignoreNames.end()) {
      return std::format("--ingest and --ignore-folder both name {}", name);
    }
  }

  auto ec = std::error_code{};
  if (!fs::exists(root, ec) || ec) {
    // A missing or unreadable root is the scan's error to report, not an
    // argument error.
    return std::nullopt;
  }

  // The pre-pass already owns the walk (which directories count, the output
  // tree excluded); validate against the names it found.
  auto const dispositions = computeFolderDispositions(root, false, {}, {});
  auto names = std::set<std::string>{};
  for (auto const& disposition: dispositions) {
    names.insert(displaytext::pathToUtf8String(disposition.name));
  }

  auto const check = [&names](std::string_view flag, std::vector<std::string> const& list)
    -> std::optional<std::string> {
    for (auto const& name: list) {
      if (!names.contains(name)) {
        return std::format("{}: no first-level folder named {}", flag, name);
      }
    }
    return std::nullopt;
  };
  if (auto const error = check("--ingest", ingestNames); error.has_value()) {
    return error;
  }
  return check("--ignore-folder", ignoreNames);
}

}  // namespace organize
