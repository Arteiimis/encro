// Tagger output data model, shared with the organize pipeline (design D3).
#pragma once

#include <string>
#include <vector>

namespace tagger {

// Danbooru tag categories used by the wd tagger vocab (selected_tags.csv).
enum class TagCategory {
  General = 0,
  Artist = 1,
  Character = 3,
  Rating = 4,
  Meta = 9,
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
