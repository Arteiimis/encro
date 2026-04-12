#pragma once

#include <string>

namespace appentry {

auto helpIntroLine() -> std::string;

auto run(int argc, char* argv[]) -> int;

}  // namespace appentry
