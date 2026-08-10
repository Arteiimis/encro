#!/usr/bin/env bash
# Shared Linux toolchain setup for CI (ci.yml) and devcontainers (.devcontainer).
# Keeps the compiler/apt environment identical everywhere.
set -e

sudo apt-get update
sudo apt-get install -y wget ca-certificates

# clang-18's __cpp_concepts=201907L makes libstdc++ <expected> empty
# (fixed in clang 19, PR #87998); ubuntu 24.04 only ships clang-18, so use
# apt.llvm.org's clang-19.
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null
echo "deb https://apt.llvm.org/noble/ llvm-toolchain-noble-19 main" | sudo tee /etc/apt/sources.list.d/llvm-toolchain.list
sudo apt-get update
sudo apt-get install -y clang-19 g++-14 llvm-19 lld-19 ffmpeg

# Same-version llvm tools for the coverage plugin (looks up unversioned names)
sudo ln -sf /usr/bin/ld.lld-19 /usr/local/bin/ld.lld
sudo ln -sf /usr/bin/llvm-profdata-19 /usr/local/bin/llvm-profdata
sudo ln -sf /usr/bin/llvm-cov-19 /usr/local/bin/llvm-cov

# clang-19 from apt.llvm.org registers no /usr/bin/clang on a fresh system
# (GitHub runner images pre-register it); do it explicitly for both CI and WSL
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-19 110
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-19 110

# clang pairs with libstdc++-13 by default (missing std::print / views::enumerate);
# switch gcc/g++ to 14 so clang and xmake probe libstdc++-14
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100
sudo update-alternatives --set gcc /usr/bin/gcc-14
sudo update-alternatives --set g++ /usr/bin/g++-14

# Package builds (boost's CMake etc.) also link with lld: thin-LTO symbols get
# pruned under GNU ld. CI exports via GITHUB_ENV, containers via profile.
if [ -n "$GITHUB_ENV" ]; then
  echo "LDFLAGS=-fuse-ld=lld" >> "$GITHUB_ENV"
else
  echo 'export LDFLAGS=-fuse-ld=lld' >> ~/.bashrc
fi
