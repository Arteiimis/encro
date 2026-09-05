// Downloader paths against a loopback fake server (task 2.4): primary ->
// mirror fallback, checksum rejection, presence checks, cuDNN driver gate.
#include "organize/sha256.h"
#include "tagger/model_store.h"

#include "test_utils.h"

#include <httplib.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

constexpr auto kGoodBody = std::string_view{"hello-tagged-world"};

// Loopback fake HTTP server; port resolves after bind_to_any_port.
class FakeServer {
public:
  FakeServer() {
    server_.Get("/good/file.bin", [](httplib::Request const&, httplib::Response& res) {
      res.set_content(kGoodBody.data(), kGoodBody.size(), "application/octet-stream");
    });
    server_.Get("/missing/file.bin", [](httplib::Request const&, httplib::Response& res) {
      res.status = 404;
    });
    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread{[this]() { server_.listen_after_bind(); }};
  }

  ~FakeServer() {
    server_.stop();
    if (thread_.joinable()) { thread_.join(); }
  }

  auto url() const -> std::string { return "http://127.0.0.1:" + std::to_string(port_); }

private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
};

auto goodFile() -> tagger::RemoteFile {
  return tagger::RemoteFile{
    .logical = "test/model.bin",
    .urlPath = "/good/file.bin",
    .size = kGoodBody.size(),
    .sha256 = organize::sha256Hex(kGoodBody),
  };
}

}  // namespace

TEST_CASE("downloadFile installs a verified file", "[tagger]") {
  auto server = FakeServer{};
  auto temp = TempDir{};

  auto const installed =
    tagger::downloadFile(temp.path, goodFile(), server.url(), server.url());
  REQUIRE(installed.has_value());
  CHECK(installed->filename() == "file.bin");
  CHECK(testutils::readTextFile(*installed) == kGoodBody);
}

TEST_CASE("downloadFile falls back to the mirror when the primary fails", "[tagger]") {
  auto mirror = FakeServer{};
  auto temp = TempDir{};

  // Port 1 is never listening: primary connection must fail over.
  auto const installed =
    tagger::downloadFile(temp.path, goodFile(), "http://127.0.0.1:1", mirror.url());
  REQUIRE(installed.has_value());
  CHECK(testutils::readTextFile(*installed) == kGoodBody);
}

TEST_CASE("downloadFile reports failure when both endpoints miss", "[tagger]") {
  auto server = FakeServer{};
  auto temp = TempDir{};
  auto missing = goodFile();
  missing.urlPath = "/missing/file.bin";
  missing.sha256 = "";

  auto const installed =
    tagger::downloadFile(temp.path, missing, server.url(), server.url());
  REQUIRE(!installed.has_value());
}

TEST_CASE("downloadFile rejects checksum mismatch after bounded retries", "[tagger]") {
  auto server = FakeServer{};
  auto temp = TempDir{};
  auto corrupt = goodFile();
  corrupt.sha256 = organize::sha256Hex("different-bytes");

  auto const installed =
    tagger::downloadFile(temp.path, corrupt, server.url(), server.url());
  REQUIRE(!installed.has_value());
  CHECK(installed.error().find("checksum mismatch") != std::string::npos);
}

TEST_CASE("allFilesPresent checks existence and size", "[tagger]") {
  auto temp = TempDir{};
  auto files = std::vector{goodFile()};

  CHECK(!tagger::allFilesPresent(temp.path, files));
  testutils::writeTextFile(temp.path / "file.bin", kGoodBody);
  CHECK(tagger::allFilesPresent(temp.path, files));

  files.front().size = kGoodBody.size() + 1;
  CHECK(!tagger::allFilesPresent(temp.path, files));
}

TEST_CASE("ensureCudnn skips when no NVIDIA driver is reported", "[tagger]") {
  auto temp = TempDir{};
  auto const result = tagger::ensureCudnn(temp.path, false);
  REQUIRE(result.has_value());
  CHECK(!result.value());
  CHECK(fs::is_empty(temp.path));
}
