#include "app/app_entry.h"
#include "infra/crash_runtime.h"

#include <exception>

int main(int argc, char* argv[]) {
  crash::installHandlers();

  try {
    return appentry::run(argc, argv);
  } catch (std::exception const& ex) {
    crash::reportCaughtException("unhandled exception in main", ex);
    return 1;
  } catch (...) {
    crash::reportUnknownException("unhandled exception in main");
    return 1;
  }
}
