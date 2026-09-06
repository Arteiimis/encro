// English folder-name enforcement: local sanitization of model-proposed
// names, deterministic fallbacks, and distinct-cluster collision suffixes
// (design D6, spec "All folder names are English and sanitized").
#pragma once

#include <set>
#include <string>
#include <string_view>

namespace organize {

// Lowercases, maps every run of characters outside [a-z0-9_] to a single
// underscore, trims separators, and caps at 48 chars. Returns "" when nothing
// usable remains (caller decides the fallback).
auto sanitizeCharacterName(std::string const& raw) -> std::string;

// Deterministic placeholder "char_<8 hex>" derived from the cluster seed.
auto fallbackCharacterName(std::string_view seed) -> std::string;

struct UniqueName {
  std::string name;
  bool renamed = false;  // a _2/_3... suffix was applied
};

// First-come assignment over a used-name registry: collisions get _2/_3...
// suffixes. Returns the assigned name and registers it.
auto assignUniqueFolderName(std::string base, std::set<std::string>& used) -> UniqueName;

}  // namespace organize
