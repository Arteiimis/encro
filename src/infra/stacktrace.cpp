#include "infra/stacktrace.h"

#include <format>

#if defined(__has_include)
  #if __has_include(<stacktrace>) && defined(__cpp_lib_stacktrace)
    #include <stacktrace>
    #define ENCRO_HAS_STD_STACKTRACE 1
  #endif
#endif

#if !defined(ENCRO_HAS_STD_STACKTRACE)
  #include <boost/stacktrace.hpp>  // IWYU pragma: keep -- Linux-only (guarded by !_WIN32)
#endif

namespace crash {

auto captureStacktrace(std::size_t skipFrames, std::size_t maxFrames)
  -> std::vector<std::string> {
  auto frames = std::vector<std::string>{};

#if defined(ENCRO_HAS_STD_STACKTRACE)
  auto trace = std::stacktrace::current(skipFrames + 1, maxFrames);
  frames.reserve(trace.size());
  for (auto const& entry: trace) { frames.push_back(std::to_string(entry)); }
#else
  auto trace = boost::stacktrace::stacktrace(skipFrames + 1, maxFrames);
  frames.reserve(trace.size());
  for (std::size_t i = 0; i < trace.size(); ++i) {
    frames.push_back(boost::stacktrace::to_string(trace[i]));
  }
#endif

  return frames;
}

auto formatStacktrace(std::vector<std::string> const& frames) -> std::string {
  if (frames.empty()) { return "<empty stacktrace>"; }

  auto out = std::string{};
  out.reserve(frames.size() * 48);
  for (std::size_t i = 0; i < frames.size(); ++i) {
    out += std::format("  #{:02d} {}\n", i, frames[i]);
  }
  return out;
}

}  // namespace crash
