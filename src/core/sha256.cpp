#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <format>
#include <iterator>
#include <string>

namespace core {

namespace {

constexpr auto kRoundConstants = std::array<std::uint32_t, 64>{
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
  0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
  0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
  0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
  0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
  0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
  0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
  0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

auto rotr(std::uint32_t value, unsigned bits) -> std::uint32_t {
  return (value >> bits) | (value << (32 - bits));
}

void processBlock(std::array<std::uint32_t, 8>& state, std::uint8_t const* block) {
  auto w = std::array<std::uint32_t, 64>{};
  for (auto index = std::size_t{0}; index < 16; ++index) {
    w[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24U)
      | (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16U)
      | (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8U)
      | static_cast<std::uint32_t>(block[index * 4 + 3]);
  }
  for (auto index = 16; index < 64; ++index) {
    auto const s0 =
      rotr(w[index - 15], 7) ^ rotr(w[index - 15], 18) ^ (w[index - 15] >> 3U);
    auto const s1 =
      rotr(w[index - 2], 17) ^ rotr(w[index - 2], 19) ^ (w[index - 2] >> 10U);
    w[index] = w[index - 16] + s0 + w[index - 7] + s1;
  }

  auto [a, b, c, d, e, f, g, h] = state;
  for (auto index = std::size_t{0}; index < 64; ++index) {
    auto const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    auto const ch = (e & f) ^ (~e & g);
    auto const temp1 = h + s1 + ch + kRoundConstants[index] + w[index];
    auto const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    auto const maj = (a & b) ^ (a & c) ^ (b & c);
    auto const temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

}  // namespace

auto hexOf(std::array<std::uint32_t, 8> const& state) -> std::string {
  auto hex = std::string{};
  hex.reserve(64);
  std::format_to(
    std::back_inserter(hex),
    "{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}",
    state[0],
    state[1],
    state[2],
    state[3],
    state[4],
    state[5],
    state[6],
    state[7]
  );
  return hex;
}

auto digest(std::string_view bytes) -> std::string {
  auto state = std::array<std::uint32_t, 8>{
    0x6a09e667u,
    0xbb67ae85u,
    0x3c6ef372u,
    0xa54ff53au,
    0x510e527fu,
    0x9b05688cu,
    0x1f83d9abu,
    0x5be0cd19u,
  };

  auto const fullBlocks = bytes.size() / 64;
  for (auto block = std::size_t{0}; block < fullBlocks; ++block) {
    auto chunk = std::array<std::uint8_t, 64>{};
    std::memcpy(chunk.data(), bytes.data() + block * 64, 64);
    processBlock(state, chunk.data());
  }

  // Final block: tail bytes, 0x80, zero padding, 64-bit big-endian bit length.
  auto tail = std::array<std::uint8_t, 128>{};
  auto const remainder = bytes.size() % 64;
  std::memcpy(tail.data(), bytes.data() + fullBlocks * 64, remainder);
  tail[remainder] = 0x80;
  auto const tailLength = remainder <= 55 ? std::size_t{64} : std::size_t{128};
  auto const bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
  for (auto index = std::size_t{0}; index < 8; ++index) {
    tail[tailLength - 1 - index] = static_cast<std::uint8_t>(bitLength >> (8 * index));
  }
  processBlock(state, tail.data());
  if (tailLength == 128) { processBlock(state, tail.data() + 64); }
  return hexOf(state);
}

auto finishSha256(
  std::array<std::uint32_t, 8>& state,
  std::uint8_t const* tailBytes,
  std::size_t remainder,
  std::uint64_t totalBytes
) -> std::string {
  auto tail = std::array<std::uint8_t, 128>{};
  std::memcpy(tail.data(), tailBytes, remainder);
  tail[remainder] = 0x80;
  auto const tailLength = remainder <= 55 ? std::size_t{64} : std::size_t{128};
  auto const bitLength = totalBytes * 8;
  for (auto index = std::size_t{0}; index < 8; ++index) {
    tail[tailLength - 1 - index] = static_cast<std::uint8_t>(bitLength >> (8 * index));
  }
  processBlock(state, tail.data());
  if (tailLength == 128) { processBlock(state, tail.data() + 64); }
  return hexOf(state);
}

auto sha256Hex(std::string_view bytes) -> std::string {
  return digest(bytes);
}

auto sha256File(std::filesystem::path const& path) -> std::string {
  auto file = std::ifstream{path, std::ifstream::binary};
  if (!file.is_open()) { return {}; }
  auto state = std::array<std::uint32_t, 8>{
    0x6a09e667u,
    0xbb67ae85u,
    0x3c6ef372u,
    0xa54ff53au,
    0x510e527fu,
    0x9b05688cu,
    0x1f83d9abu,
    0x5be0cd19u,
  };
  auto buffer = std::array<char, 8192>{};
  auto total = std::uint64_t{0};
  auto pending = std::array<std::uint8_t, 64>{};
  auto pendingSize = std::size_t{0};
  while (
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))
    || file.gcount() > 0
  ) {
    auto const got = static_cast<std::size_t>(file.gcount());
    if (got == 0) { break; }
    total += got;
    // Top up pending to a whole 64-byte block first.
    auto offset = std::size_t{0};
    if (pendingSize > 0) {
      auto const take = std::min(64 - pendingSize, got);
      std::memcpy(pending.data() + pendingSize, buffer.data(), take);
      pendingSize += take;
      offset = take;
      if (pendingSize < 64) { continue; }
      processBlock(state, pending.data());
      pendingSize = 0;
    }
    auto const full = (got - offset) / 64;
    for (auto block = std::size_t{0}; block < full; ++block) {
      processBlock(
        state,
        reinterpret_cast<std::uint8_t const*>(buffer.data()) + offset + block * 64
      );
    }
    pendingSize = got - offset - full * 64;
    if (pendingSize > 0) {
      std::memcpy(
        pending.data(),
        reinterpret_cast<std::uint8_t const*>(buffer.data()) + offset + full * 64,
        pendingSize
      );
    }
  }
  return finishSha256(state, pending.data(), pendingSize, total);
}

}  // namespace core
