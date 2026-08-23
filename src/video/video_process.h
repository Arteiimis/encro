#pragma once

#include "core/app_context.h"

#include <filesystem>
#include <span>

namespace fs = std::filesystem;

int handleSingleFileEncoding(appctx::AppContext& ctx, fs::path const& videoPath);

int handleMultiFileEncoding(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
);

int handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath);
