#pragma once

#include "core/app_context.h"

#include <filesystem>
#include <span>

namespace fs = std::filesystem;

auto handleSingleFileEncoding(appctx::AppContext& ctx, fs::path const& videoPath) -> int;

auto handleMultiFileEncoding(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) -> int;

auto handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath) -> int;
