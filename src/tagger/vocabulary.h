// Tagger vocabulary (selected_tags.csv) loading: the CSV row order is the
// model output order, so the vocabulary is the output-column map (design D3).
#pragma once

#include "tagger/tagger_types.h"

#include "core/error_handle.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace tagger {

struct VocabEntry {
  std::string name;
  TagCategory category = TagCategory::General;
};

// Rows in file order (== model output column order).
using Vocabulary = std::vector<VocabEntry>;

// Parses a wd tagger selected_tags.csv (columns: tag_id,name,category,...).
// Errors when the file is missing or a row lacks name/category.
auto loadVocabulary(fs::path const& csvPath) -> eh::Result<Vocabulary>;

}  // namespace tagger
