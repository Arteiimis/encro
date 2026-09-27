#include "core/file_write.h"

#include "test_utils.h"

#include <filesystem>

TEST_CASE("writeStagingFile reports the staging write outcome", "[file-write]") {
  auto temp = TempDir{};

  SECTION("a writable path reports Written and lands the content") {
    auto const stagingPath = temp.path / "staging.json";
    CHECK(
      fileio::writeStagingFile(stagingPath, R"({"stage":"partial"})")
      == fileio::StagingStatus::Written
    );
    CHECK(testutils::readTextFile(stagingPath) == R"({"stage":"partial"})");
  }

  SECTION("an unopenable path reports OpenFailed") {
    // WriteFailed has no portable trigger (it needs a flush failure, which
    // takes a full or read-only filesystem), so it stays uncovered here.
    CHECK(
      fileio::writeStagingFile(temp.path, "unwritable")
      == fileio::StagingStatus::OpenFailed
    );
  }
}
