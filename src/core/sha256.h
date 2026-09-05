// Compact SHA-256 for content-hash keys (cache, model downloads).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace core {

// Lowercase hex SHA-256 digest of the given bytes.
auto sha256Hex(std::string_view bytes) -> std::string;

// Streaming digest of a file's contents; "" when the file is unreadable.
// Avoids buffering large files (the cuDNN archive is ~550 MB).
auto sha256File(std::filesystem::path const& path) -> std::string;

}  // namespace core
