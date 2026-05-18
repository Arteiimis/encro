#include "app/pipeline.h"

#include "core/app_context.h"
#include "core/error_handle.h"
#include "core/job_state.h"
#include "core/job_state_detail.h"
#include "core/media_scanner.h"
#include "infra/terminal.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace std::literals;
using enum terminal::MessageKind;

namespace pipeline {
namespace {

// ── Helpers ──────────────────────────────────────────────────────────

auto formatSize(std::uintmax_t bytes) -> std::string {
  constexpr auto kKB = std::uintmax_t{1024};
  constexpr auto kMB = kKB * 1024;
  constexpr auto kGB = kMB * 1024;

  if (bytes >= kGB) {
    auto const gb = static_cast<double>(bytes) / static_cast<double>(kGB);
    return std::format("{:.2f} GB", gb);
  }
  if (bytes >= kMB) {
    auto const mb = static_cast<double>(bytes) / static_cast<double>(kMB);
    return std::format("{:.2f} MB", mb);
  }
  if (bytes >= kKB) {
    auto const kb = static_cast<double>(bytes) / static_cast<double>(kKB);
    return std::format("{:.2f} KB", kb);
  }
  return std::format("{} B", bytes);
}

auto extensionsForType(std::string_view processType)
  -> std::vector<std::string_view> {
  if (processType == "picture") {
    return {".jpg"sv, ".jpeg"sv, ".png"sv, ".bmp"sv, ".tiff"sv, ".gif"sv, ".heic"sv};
  }
  // video (default)
  return {".mp4"sv, ".mkv"sv, ".avi"sv, ".mov"sv, ".flv"sv, ".wmv"sv};
}

// ── Validation layer ─────────────────────────────────────────────────

struct ValidationResult {
  std::size_t failures = 0;
};

auto runValidation(appctx::AppContext const& ctx) -> ValidationResult {
  terminal::println(Heading, "── Dry-Run: Validation ──");
  terminal::println(Plain, "");

  auto result = ValidationResult{};

  // 1. ffmpeg path
  if (!ctx.config.packOnly) {
    auto const ffmpegOk = ctx.toolchain.ffmpegPath.has_value();
    if (ffmpegOk) {
      terminal::println(
        Success, "  [OK] ffmpeg: {}", terminal::path(ctx.toolchain.ffmpegPath.value())
      );
    } else {
      terminal::println(Error, "  [FAIL] ffmpeg: not found");
      ++result.failures;
    }

    // 2. ffprobe path
    auto const ffprobeOk = ctx.toolchain.ffprobePath.has_value();
    if (ffprobeOk) {
      terminal::println(
        Success,
        "  [OK] ffprobe: {}",
        terminal::path(ctx.toolchain.ffprobePath.value())
      );
    } else {
      terminal::println(Error, "  [FAIL] ffprobe: not found");
      ++result.failures;
    }
  }

  // 3. Input path exists and readable
  std::error_code ec;
  auto const inputExists = fs::exists(ctx.config.inputPath, ec);
  if (inputExists && !ec) {
    terminal::println(Success, "  [OK] Input path: {}", terminal::path(ctx.config.inputPath));
  } else {
    terminal::println(Error, "  [FAIL] Input path does not exist: {}", terminal::path(ctx.config.inputPath));
    ++result.failures;
  }

  // 4. Output parent dir writable
  if (ctx.config.outputPath.has_value()) {
    auto const outputPath = ctx.config.outputPath.value();
    auto const parentPath = outputPath.parent_path();

    // Check if output directory already exists
    if (fs::exists(outputPath, ec) && !ec) {
      terminal::println(
        Warning,
        "  [WARN] Output directory already exists — files may be overwritten: {}",
        terminal::path(outputPath)
      );
    }

    // Check parent directory writability
    std::error_code parentEc;
    auto const parentExists = fs::exists(parentPath, parentEc);
    if (parentExists && !parentEc) {
      auto const perms = fs::status(parentPath, ec).permissions();
      auto const writable =
        (perms & fs::perms::owner_write) != fs::perms::none;
      if (writable) {
        terminal::println(Success, "  [OK] Output parent writable: {}", terminal::path(parentPath));
      } else {
        terminal::println(Error, "  [FAIL] Output parent not writable: {}", terminal::path(parentPath));
        ++result.failures;
      }
    } else {
      // Parent doesn't exist — warn but allow (can be created at runtime)
      terminal::println(
        Warning,
        "  [WARN] Output parent does not exist (will be created at runtime): {}",
        terminal::path(parentPath)
      );
    }
  }

  terminal::println(Plain, "");
  return result;
}

// ── Scan layer ───────────────────────────────────────────────────────

struct ScanResult {
  std::size_t fileCount = 0;
  std::uintmax_t totalSize = 0;
  bool failed = false;
};

auto runScan(appctx::AppContext const& ctx) -> ScanResult {
  terminal::println(Heading, "── Dry-Run: Scan ──");
  terminal::println(Plain, "");

  auto result = ScanResult{};

  if (!ctx.config.packOnly) {
    // Full recursive file scan by extensions
    auto const extensions = extensionsForType(ctx.config.processType);
    auto const files = media::scanByExtensions(
      ctx.config.inputPath,
      std::span{extensions},
      ctx.config.recursive
    );

    result.fileCount = files.size();
    std::error_code ec;
    for (auto const& file: files) {
      auto const sz = fs::file_size(file, ec);
      if (!ec) { result.totalSize += sz; }
    }

    terminal::println(Info, "  File count: {}", terminal::count(result.fileCount));
    terminal::println(Info, "  Total size: {}", formatSize(result.totalSize));

    if (result.fileCount == 0) {
      terminal::println(Warning, "  No media files found in input path.");
    }
  } else {
    terminal::println(Info, "  Mode: pack-only (no media scan)");
  }

  // Job state info (--resume)
  if (ctx.config.resumeState) {
    auto const stateFilePath = ctx.config.stateFilePath.has_value()
      ? ctx.config.stateFilePath.value()
      : jobstate::buildDefaultStateFilePath(ctx.config);

    std::error_code ec;
    if (fs::exists(stateFilePath, ec) && !ec) {
      auto const snapshotRes = jobstate::detail::loadSnapshot(stateFilePath);
      if (snapshotRes.has_value()) {
        auto const& tasks = snapshotRes.value().tasks;
        auto succeeded = std::size_t{0};
        auto pending = std::size_t{0};
        auto failed = std::size_t{0};
        auto running = std::size_t{0};
        for (auto const& task: tasks) {
          switch (task.status) {
            case jobstate::TaskStatus::Succeeded: ++succeeded; break;
            case jobstate::TaskStatus::Pending: ++pending; break;
            case jobstate::TaskStatus::Failed: ++failed; break;
            case jobstate::TaskStatus::Running: ++running; break;
            case jobstate::TaskStatus::Interrupted: break;
          }
        }
        terminal::println(Info, "  Job state: {}", terminal::path(stateFilePath));
        terminal::println(Info, "    Completed: {}", terminal::count(succeeded));
        terminal::println(Info, "    Pending:   {}", terminal::count(pending));
        if (failed > 0) {
          terminal::println(Warning, "    Failed:    {}", terminal::count(failed));
        }
        if (running > 0) {
          terminal::println(Info, "    Running:   {}", terminal::count(running));
        }
      } else {
        terminal::println(Warning, "  Job state: failed to read — {}", snapshotRes.error());
      }
    } else {
      terminal::println(Info, "  Job state: no resumable state found");
    }
  }

  terminal::println(Plain, "");
  return result;
}

// ── Plan layer ───────────────────────────────────────────────────────

auto runPlan(appctx::AppContext const& ctx, ScanResult const& scan) -> bool {
  terminal::println(Heading, "── Dry-Run: Plan ──");
  terminal::println(Plain, "");

  auto const workerCount = ctx.config.maxParallelJobs.value_or(10);

  terminal::println(Info, "  Encodes: {}", terminal::count(scan.fileCount));
  terminal::println(Info, "  Format:  {}", ctx.config.outputFormat);
  terminal::println(Info, "  Workers: {}", terminal::count(workerCount));

  // Pack archive estimate
  if (ctx.config.packOutput || ctx.config.packOnly) {
    constexpr auto kFilesPerArchive = std::size_t{10};
    auto const archiveCount = scan.fileCount > 0
      ? static_cast<std::size_t>(std::ceil(
          static_cast<double>(scan.fileCount) / static_cast<double>(kFilesPerArchive)
        ))
      : std::size_t{0};
    terminal::println(Info, "  Archives (estimated): ~{}", terminal::count(archiveCount));
    terminal::println(Hint, "    (estimate based on ~{} files per archive)", kFilesPerArchive);
  }

  // Output directory warning
  if (ctx.config.outputPath.has_value()) {
    std::error_code ec;
    if (fs::exists(ctx.config.outputPath.value(), ec) && !ec) {
      terminal::println(
        Warning,
        "  Output directory exists — encoded files may overwrite existing files"
      );
    }
  }

  terminal::println(Plain, "");
  terminal::println(Success, "Dry-run complete. No files were written.");
  return false;
}

}  // namespace

auto runDryRun(appctx::AppContext const& ctx) -> eh::Result<int> {
  // Layer 1: Validation
  auto validation = runValidation(ctx);
  if (validation.failures > 0) {
    return eh::makeError("Validation failed with {} check(s).", validation.failures);
  }

  // Layer 2: Scan
  auto scan = runScan(ctx);
  if (scan.failed) {
    return eh::makeError("Scan failed.");
  }

  // Layer 3: Plan
  auto planFailed = runPlan(ctx, scan);
  if (planFailed) {
    return eh::makeError("Plan generation failed.");
  }

  return 0;
}

}  // namespace pipeline
