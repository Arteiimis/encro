#include "infra/open_file.h"

#if defined(_WIN32)
  #include <windows.h>  // IWYU pragma: keep -- Windows-only (guarded by _WIN32)
  #include <shellapi.h>

  #include <cstdint>
#endif

namespace fs = std::filesystem;

namespace openfile {

bool openWithDefaultApp(fs::path const& path) {
#if defined(_WIN32)
  auto const wide = path.wstring();
  auto const result = reinterpret_cast<std::intptr_t>(
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
  );
  return result > 32;
#else
  // No portable shell-open helper on POSIX; callers warn instead of failing.
  return false;
#endif
}

}  // namespace openfile
