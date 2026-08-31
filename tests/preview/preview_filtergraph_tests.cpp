#include "preview/preview_filtergraph.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Mirrors the platform font choice in preview_filtergraph.cpp; the Windows
// path keeps the \: escaping that survives drawtext's double tokenization.
auto fontFile() -> std::string {
#if defined(_WIN32)
  return "C\\:/Windows/Fonts/arial.ttf";
#else
  return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
}

auto makeProbe(int width, int height, double fps = 30.0, bool hasAudio = false)
  -> preview::VideoProbe {
  auto probe = preview::VideoProbe{};
  probe.width = width;
  probe.height = height;
  probe.fps = fps;
  probe.durationUs = 100'000'000;
  probe.hasAudio = hasAudio;
  return probe;
}

auto makeWindow(std::uint64_t startUs, std::optional<double> score = std::nullopt)
  -> preview::Window {
  return preview::Window{
    .startUs = startUs,
    .durationUs = 10'000'000,
    .score = score,
  };
}

}  // namespace

TEST_CASE("filtergraph fps-normalizes scales and labels both panes", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(1280, 720);
  spec.encoded = makeProbe(1920, 1080);
  spec.windows = {makeWindow(0, 95.2), makeWindow(50'000'000, 88.1)};

  auto const graph = preview::buildPreviewFiltergraph(spec);

  // Inputs are pre-cut windows: original window i is input i, encoded
  // window i is input windowCount + i. No trims anywhere.
  CHECK(
    graph.find(
      "[0:v]setpts=PTS-STARTPTS,"
      "fps=30.000000,scale=1280:720:force_original_aspect_ratio=decrease,"
      "pad=1280:720:(ow-iw)/2:(oh-ih)/2,format=yuv420p,"
      "drawtext=text='ORIGINAL':fontfile='"
      + fontFile()
      + "':"
        "fontsize=24:fontcolor=white:x=10:y=10:box=1:boxcolor=black@0.5:boxborderw=4,"
        "drawtext=text='00\\:00-00\\:10, VMAF "
        "95.2':fontfile='"
      + fontFile()
      + "':"
        "fontsize=18:fontcolor=white:x=10:y=38:box=1:boxcolor=black@0.5:boxborderw=4[o0]"
    )
    != std::string::npos
  );

  // Encoded pane has no segment label, only ENCODED.
  CHECK(
    graph.find(
      "[3:v]setpts=PTS-STARTPTS,"
      "fps=30.000000,scale=1280:720:force_original_aspect_ratio=decrease,"
      "pad=1280:720:(ow-iw)/2:(oh-ih)/2,format=yuv420p,"
      "drawtext=text='ENCODED':fontfile='"
      + fontFile()
      + "':"
        "fontsize=24:fontcolor=white:x=10:y=10:box=1:boxcolor=black@0.5:boxborderw=4[e1]"
    )
    != std::string::npos
  );
  CHECK(graph.find("trim=") == std::string::npos);

  CHECK(graph.find("[o0][e0]hstack[h0];") != std::string::npos);
  CHECK(graph.find("[o1][e1]hstack[h1];") != std::string::npos);
  CHECK(graph.find("[h0][h1]concat=n=2:v=1:a=0[vout]") != std::string::npos);
}

TEST_CASE("filtergraph carries windowed audio when the original has audio", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360, 24.0, true);
  spec.encoded = makeProbe(640, 360, 24.0, true);
  spec.windows = {makeWindow(0, 90.0), makeWindow(40'000'000, 91.0)};

  auto const graph = preview::buildPreviewFiltergraph(spec);

  CHECK(
    graph.find(
      "[0:a]asetpts=PTS-STARTPTS[a0];"
      "[1:a]asetpts=PTS-STARTPTS[a1];"
      "[a0][a1]concat=n=2:v=0:a=1[aout]"
    )
    != std::string::npos
  );
}

TEST_CASE("filtergraph omits audio chains for silent originals", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360);
  spec.encoded = makeProbe(640, 360);
  spec.windows = {makeWindow(0)};

  auto const graph = preview::buildPreviewFiltergraph(spec);
  CHECK(graph.find("[0:a]") == std::string::npos);
  CHECK(graph.find("a=0[aout]") == std::string::npos);
}

TEST_CASE("filtergraph rounds odd dimensions down to even", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(1279, 721);
  spec.encoded = makeProbe(1921, 1081);
  spec.windows = {makeWindow(0)};

  auto const graph = preview::buildPreviewFiltergraph(spec);
  CHECK(
    graph.find("scale=1278:720:force_original_aspect_ratio=decrease") != std::string::npos
  );
  CHECK(graph.find("pad=1278:720:") != std::string::npos);
}

TEST_CASE("filtergraph omits the fps filter when the rate is unknown", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360, 0.0);
  spec.encoded = makeProbe(640, 360, 0.0);
  spec.windows = {makeWindow(0)};

  auto const graph = preview::buildPreviewFiltergraph(spec);
  CHECK(graph.find("fps=") == std::string::npos);
}

TEST_CASE("filtergraph uses ssim labels and omits scores without values", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360);
  spec.encoded = makeProbe(640, 360);
  auto window = makeWindow(0, 0.981);
  window.metric = videoquality::QualityMetric::Ssim;
  spec.windows = {window};

  auto const graph = preview::buildPreviewFiltergraph(spec);
  CHECK(graph.find("text='00\\:00-00\\:10, SSIM 0.981'") != std::string::npos);

  auto manual = spec;
  manual.windows = {makeWindow(0, std::nullopt)};
  auto const manualGraph = preview::buildPreviewFiltergraph(manual);
  CHECK(manualGraph.find("text='00\\:00-00\\:10'") != std::string::npos);
  CHECK(manualGraph.find("VMAF") == std::string::npos);
}

TEST_CASE("filtergraph renders xpsnr scores as signed dB", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360);
  spec.encoded = makeProbe(640, 360);
  auto window = makeWindow(0, 41.05);
  window.metric = videoquality::QualityMetric::Xpsnr;
  spec.windows = {window};

  auto const graph = preview::buildPreviewFiltergraph(spec);
  CHECK(graph.find("text='00\\:00-00\\:10, XPSNR 41.05 dB'") != std::string::npos);
}

TEST_CASE(
  "preview command encodes with hevc_nvenc by default and aac audio",
  "[preview]"
) {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360, 30.0, true);
  spec.encoded = makeProbe(640, 360, 30.0, true);
  spec.windows = {makeWindow(0, 90.0)};

  auto const cmd =
    preview::buildPreviewCommand("ffmpeg", "a.mp4", {"b.mp4"}, spec, "out.mp4");

  CHECK(
    cmd.find(
      "-ss 0.000000 -t 10.000000 -i \"a.mp4\""
      " -ss 0.000000 -t 10.000000 -i \"b.mp4\""
    )
    != std::string::npos
  );
  CHECK(cmd.find("-filter_complex \"") != std::string::npos);
  CHECK(cmd.find("-map \"[vout]\"") != std::string::npos);
  CHECK(cmd.find("-map \"[aout]\" -c:a aac -b:a 192k") != std::string::npos);
  CHECK(
    cmd.find(
      "-c:v hevc_nvenc -preset p5 -rc vbr -cq 14 -b:v 0 -pix_fmt yuv420p -tag:v hvc1"
    )
    != std::string::npos
  );
  CHECK(cmd.find("\"out.mp4\"") != std::string::npos);
}

TEST_CASE("preview command honors the configured codec", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360);
  spec.encoded = makeProbe(640, 360);
  spec.windows = {makeWindow(0)};

  auto const x264 = preview::buildPreviewCommand(
    "ffmpeg",
    "a.mp4",
    {"b.mp4"},
    spec,
    "out.mp4",
    preview::PreviewEncoderSettings{.codec = "libx264"}
  );
  CHECK(
    x264.find("-c:v libx264 -crf 14 -preset veryfast -pix_fmt yuv420p")
    != std::string::npos
  );

  auto const x265 = preview::buildPreviewCommand(
    "ffmpeg",
    "a.mp4",
    {"b.mp4"},
    spec,
    "out.mp4",
    preview::PreviewEncoderSettings{.codec = "libx265"}
  );
  CHECK(x265.find("-c:v libx265 -crf 14 -preset veryfast") != std::string::npos);

  auto const nvenc = preview::buildPreviewCommand(
    "ffmpeg",
    "a.mp4",
    {"b.mp4"},
    spec,
    "out.mp4",
    preview::PreviewEncoderSettings{.codec = "h264_nvenc", .nvencPreset = "p6"}
  );
  CHECK(
    nvenc.find("-c:v h264_nvenc -preset p6 -rc vbr -cq 14 -b:v 0") != std::string::npos
  );
  // Only hevc_nvenc output carries the hvc1 tag.
  CHECK(nvenc.find("-tag:v hvc1") == std::string::npos);
}

TEST_CASE("preview command is silent without audio", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(640, 360);
  spec.encoded = makeProbe(640, 360);
  spec.windows = {makeWindow(0)};

  auto const cmd =
    preview::buildPreviewCommand("ffmpeg", "a.mp4", {"b.mp4"}, spec, "out.mp4");
  CHECK(cmd.find(" -an ") != std::string::npos);
  CHECK(cmd.find("-c:a") == std::string::npos);
}

TEST_CASE("segment mode maps each segment to its own input", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(1280, 720);
  spec.encoded = makeProbe(1280, 720);
  spec.windows = {makeWindow(0), makeWindow(50'000'000)};
  spec.encodedWindowsAreSegments = true;

  auto const graph = preview::buildPreviewFiltergraph(spec);

  // Window 0 uses original input [0:v] and segment input [2:v].
  CHECK(graph.find("[0:v]setpts=PTS-STARTPTS") != std::string::npos);
  CHECK(graph.find("[2:v]setpts=PTS-STARTPTS") != std::string::npos);
  // Window 1 uses original input [1:v] and segment input [3:v].
  CHECK(graph.find("[1:v]setpts=PTS-STARTPTS") != std::string::npos);
  CHECK(graph.find("[3:v]setpts=PTS-STARTPTS") != std::string::npos);
  // No trims: every input is pre-cut to its window.
  CHECK(graph.find("trim=") == std::string::npos);
  CHECK(graph.find("[o0][e0]hstack[h0];") != std::string::npos);
  CHECK(graph.find("[o1][e1]hstack[h1];") != std::string::npos);
}

TEST_CASE("segment mode command lists every segment input", "[preview]") {
  auto spec = preview::FiltergraphSpec{};
  spec.original = makeProbe(1280, 720);
  spec.encoded = makeProbe(1280, 720);
  spec.windows = {makeWindow(0), makeWindow(50'000'000)};
  spec.encodedWindowsAreSegments = true;

  auto const cmd = preview::buildPreviewCommand(
    "ffmpeg",
    "a.mp4",
    {"seg0.ts", "seg1.ts"},
    spec,
    "out.mp4"
  );
  // Two seeked inputs for the original's two windows, then the segments.
  CHECK(
    cmd.find(
      "-ss 0.000000 -t 10.000000 -i \"a.mp4\""
      " -ss 50.000000 -t 10.000000 -i \"a.mp4\""
      " -i \"seg0.ts\" -i \"seg1.ts\""
    )
    != std::string::npos
  );
  CHECK(cmd.find("-map \"[vout]\"") != std::string::npos);
}

TEST_CASE("formatTimeRange renders mm:ss and h:mm:ss", "[preview]") {
  CHECK(preview::formatTimeRange(10'000'000, 20'000'000) == "00:10-00:30");
  CHECK(preview::formatTimeRange(25'510'000'000, 10'000'000) == "7:05:10-7:05:20");
}
