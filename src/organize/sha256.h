// Compact SHA-256 for content-hash cache keys (design D5).
#pragma once

#include <string>
#include <string_view>

namespace organize {

// Lowercase hex SHA-256 digest of the given bytes.
auto sha256Hex(std::string_view bytes) -> std::string;

}  // namespace organize
