// The engine seam: the organize pipeline classifies images through this
// interface only — onnxruntime/ffmpeg never leak past it (design D3).
#pragma once

#include "tagger/tagger_types.h"

#include "core/error_handle.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace tagger {

class TaggerEngine {
public:
  virtual ~TaggerEngine() = default;

  // Reads and classifies the image at `path`. Errors when the file cannot be
  // read/decoded; the pipeline degrades such images to uncategorized/.
  virtual auto classify(fs::path const& path) -> eh::Result<TaggerOutput> = 0;

  // Execution provider named in organize's one-line notice ("cuda"/"cpu");
  // engines without provider selection report "n/a".
  virtual auto providerName() const -> std::string { return "n/a"; }
};

}  // namespace tagger
