#pragma once

#include <exception>
#include <string_view>

namespace crash {

void installHandlers();

void reportCaughtException(std::string_view context, std::exception const& exception);

void reportUnknownException(std::string_view context);

}  // namespace crash
