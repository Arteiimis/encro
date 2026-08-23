#pragma once

#include <filesystem>

namespace openfile {

// Opens path with the system's default app (ShellExecuteW on Windows).
// Returns false when opening is unsupported or failed; callers treat that
// as "not opened" (e.g. --no-open e2e runs never reach this).
bool openWithDefaultApp(std::filesystem::path const& path);

}  // namespace openfile
