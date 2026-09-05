#include "organize/naming.h"

#include "core/collision_naming.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace organize {

namespace {

constexpr auto kMaxNameLength = std::size_t{48};

}

auto sanitizeCharacterName(std::string const& raw) -> std::string {
  auto sanitized = std::string{};
  sanitized.reserve(raw.size());

  auto lastWasSeparator = false;
  for (char const ch: raw) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      sanitized
        .push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      lastWasSeparator = false;
      continue;
    }
    if (!lastWasSeparator && !sanitized.empty()) {
      sanitized.push_back('_');
      lastWasSeparator = true;
    }
  }

  while (!sanitized.empty() && sanitized.back() == '_') { sanitized.pop_back(); }
  if (sanitized.size() > kMaxNameLength) { sanitized.resize(kMaxNameLength); }
  return sanitized;
}

auto fallbackCharacterName(std::string_view seed) -> std::string {
  return std::format("char_{:08x}", collisionnaming::fnv1a32(seed));
}

auto assignUniqueFolderName(std::string base, std::set<std::string>& used) -> UniqueName {
  if (used.insert(base).second) { return UniqueName{.name = base, .renamed = false}; }

  for (auto suffix = 2;; ++suffix) {
    auto candidate = std::format("{}_{}", base, suffix);
    if (used.insert(candidate).second) {
      return UniqueName{.name = candidate, .renamed = true};
    }
  }
}

}  // namespace organize
