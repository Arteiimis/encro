// Compact SHA-256 for content-hash keys (cache, model downloads).
#pragma once

#include <string>
#include <string_view>

namespace core {

// Lowercase hex SHA-256 digest of the given bytes.
auto sha256Hex(std::string_view bytes) -> std::string;

}  // namespace core
