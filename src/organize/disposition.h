// Folder dispositions for incremental organize (spec "Incremental organize
// over an existing folder structure"): one pass over the target's first-level
// directories decides reference / input / ignored before any scan, so every
// consumer (scan input, sampling, notices, the report) reads one list instead
// of re-deriving rules.
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace organize {

enum class DispositionKind {
  Reference,  // mirrored and taught; never scan input
  Input,      // collected recursively as scan input
  Ignored,    // never entered
};

// Why the folder got its disposition (asserted by the disposition tests;
// the report itself shows the kind and the demotion flag).
enum class DispositionReason {
  Default,   // reference default, dot-prefix rule, or the recursive all-input
  NameList,  // the built-in miscellaneous-name list
  Flag,      // --ingest / --ignore-folder
};

struct FolderDisposition {
  fs::path name;
  DispositionKind kind = DispositionKind::Reference;
  DispositionReason reason = DispositionReason::Default;
};

// Common miscellaneous folder names (design D2), in English, Simplified
// Chinese, Traditional Chinese and Japanese. Matched against the whole
// trimmed name — ASCII folded to lowercase, other scripts exact, never by
// substring, never across simplified/traditional conversion. Deliberately
// excluded as too aggressive: new, downloads, 新下载 — a deliberate staging
// folder must not be silently reorganized; --ingest is the explicit path.
inline constexpr auto kMiscFolderNames = std::array{
  // English
  std::string_view{"mix"},
  std::string_view{"mixed"},
  std::string_view{"misc"},
  std::string_view{"miscellaneous"},
  std::string_view{"assorted"},
  std::string_view{"random"},
  std::string_view{"various"},
  std::string_view{"other"},
  std::string_view{"others"},
  std::string_view{"unsorted"},
  std::string_view{"unclassified"},
  std::string_view{"uncategorized"},
  std::string_view{"unfiled"},
  std::string_view{"pending"},
  std::string_view{"todo"},
  std::string_view{"inbox"},
  std::string_view{"leftover"},
  // Simplified Chinese
  std::string_view{"杂项"},
  std::string_view{"杂图"},
  std::string_view{"混合"},
  std::string_view{"未分类"},
  std::string_view{"未整理"},
  std::string_view{"未归类"},
  std::string_view{"待分类"},
  std::string_view{"待整理"},
  std::string_view{"其他"},
  std::string_view{"其它"},
  std::string_view{"临时"},
  std::string_view{"暂存"},
  std::string_view{"新图"},
  std::string_view{"下载"},
  // Traditional Chinese (entries identical to a Simplified one are shared)
  std::string_view{"雜項"},
  std::string_view{"雜圖"},
  std::string_view{"未分類"},
  std::string_view{"未歸類"},
  std::string_view{"待分類"},
  std::string_view{"臨時"},
  std::string_view{"暫存"},
  std::string_view{"新圖"},
  std::string_view{"下載"},
  // Japanese
  std::string_view{"その他"},
  std::string_view{"雑多"},
  std::string_view{"仮置き"},
  std::string_view{"とりあえず"},
  std::string_view{"一時"},
  std::string_view{"新着"},
  std::string_view{"ミックス"},
};

bool isMiscFolderName(std::string const& name);

// The pre-pass: walks the target's first-level directories (besides the
// organized/ output tree) and assigns each exactly one disposition. A
// recursive run disposes every folder not named by ignoreNames as input —
// the whole-set behavior recursive runs have always had. Result is in
// folder-name order. An unreadable root yields an empty list; the scan
// reports the real error.
auto computeFolderDispositions(
  fs::path const& root,
  bool recursive,
  std::vector<std::string> const& ingestNames,
  std::vector<std::string> const& ignoreNames
) -> std::vector<FolderDisposition>;

// Flag validation (the command layer runs it before any scan and reports the
// result as an argument error): every name must be an existing first-level
// directory of the target besides the output tree, and no name may be given
// to both flags.
auto validateDispositionFlags(
  fs::path const& root,
  std::vector<std::string> const& ingestNames,
  std::vector<std::string> const& ignoreNames
) -> std::optional<std::string>;

}  // namespace organize
