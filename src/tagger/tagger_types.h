// Tagger output data model, shared with the organize pipeline (design D3).
#pragma once

#include <string>
#include <vector>

namespace tagger {

// Danbooru tag categories in the wd-v3 vocabulary (selected_tags.csv):
// 0 = general (appearance/subject-count tags), 4 = character, 9 = rating
// (general/sensitive/questionable/explicit). Verified against the real CSV
// during acceptance; other Danbooru categories are absent from this vocab.
enum class TagCategory {
  General = 0,
  Character = 4,
  Rating = 9,
};

// One (tag, confidence) pair from the tagger output.
struct TagScore {
  std::string tag;
  double confidence = 0.0;

  bool operator==(TagScore const&) const = default;
};

// Raw analysis output for one image: confidence for every vocabulary tag of
// the categories the pipeline consumes (artist/meta tags are dropped).
struct TaggerOutput {
  std::vector<TagScore> general;
  std::vector<TagScore> character;
  std::vector<TagScore> rating;

  bool operator==(TaggerOutput const&) const = default;
};

}  // namespace tagger
