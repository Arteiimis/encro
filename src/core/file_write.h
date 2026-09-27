#pragma once

#include <filesystem>
#include <fstream>
#include <ios>
#include <string_view>

namespace fileio {

// Outcome of staging a serialized file into a caller-chosen temp path.
enum class StagingStatus {
  Written,
  OpenFailed,
  WriteFailed
};

// Opens stagingPath with the caller's mode, writes content, flushes and closes
// it. close() stays unchecked, as every call site leaves it.
inline auto writeStagingFile(
  std::filesystem::path const& stagingPath,
  std::string_view content,
  std::ios::openmode mode = std::ios::out
) -> StagingStatus {
  auto out = std::ofstream{stagingPath, mode};
  if (!out) { return StagingStatus::OpenFailed; }

  out << content;
  out.flush();
  if (!out) { return StagingStatus::WriteFailed; }

  out.close();
  return StagingStatus::Written;
}

}  // namespace fileio
