#pragma once

#include <string>

namespace appentry {

auto helpIntroLine() -> std::string;

auto runStatus(int exitCode, bool stopRequested) -> std::string;

int run(int argc, char* argv[]);

}  // namespace appentry
