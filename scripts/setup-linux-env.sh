#!/usr/bin/env bash
# Shared Linux toolchain setup for CI (ci.yml) and devcontainers (.devcontainer).
# Keeps the compiler/apt environment identical everywhere.
set -e

# GitHub runner 的 apt 镜像（azure.archive.ubuntu.com，经 /etc/apt/apt-mirrors.txt
# mirrorlist 解析）曾在整个下午龟速：连接不挂但 ~30KB/s，两次把 install 步骤
# 拖到超时。切到官方 Fastly CDN 源（从 runner 访问稳定）。runner 是临时
# 机器，无需还原。两个候选配置文件都替换，不存在则忽略。
sudo sed -i \
  -e 's|http://azure\.archive\.ubuntu\.com|http://archive.ubuntu.com|g' \
  -e 's|https://azure\.archive\.ubuntu\.com|https://archive.ubuntu.com|g' \
  /etc/apt/apt-mirrors.txt /etc/apt/sources.list.d/ubuntu.sources 2>/dev/null || true

# apt's default is NO per-connection timeout: a stalled mirror connection
# wedges `apt-get update` forever (seen twice: all three CI jobs hung in the
# install step until the step timeout killed them). Bound each connection and
# let apt retry so a flaky network fails fast instead of hanging.
APT_OPTS="-o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 -o Acquire::Retries=3"

sudo apt-get update $APT_OPTS
sudo apt-get install -y $APT_OPTS wget ca-certificates

# clang-18's __cpp_concepts=201907L makes libstdc++ <expected> empty
# (fixed in clang 19, PR #87998); ubuntu 24.04 only ships clang-18, so use
# apt.llvm.org's clang-19.
#
# Fetch the signing key and verify its fingerprint against a stale/truncated
# proxy-CD5 copy (apt fails with NO_PUBKEY; seen once on one runner).
# Note: gpg --keyring does NOT parse ASCII-armor files, so verify the raw
# stream via --show-keys before tee'ing it into trusted.gpg.d.
# --timeout: a stalled connection would otherwise hang the step forever
# (wget's default is no timeout); seen wedging all three CI jobs.
KEYRING=/etc/apt/trusted.gpg.d/apt.llvm.org.asc
FPR=15CF4D18AF4F7421
for attempt in 1 2 3; do
  data=$(wget -qO- --timeout=30 https://apt.llvm.org/llvm-snapshot.gpg.key) || { sleep 5; continue; }
  if printf '%s' "$data" | gpg --batch --show-keys 2>/dev/null | grep -q "$FPR"; then
    printf '%s' "$data" | sudo tee "$KEYRING" > /dev/null
    break
  fi
  echo "apt.llvm.org key missing $FPR (attempt $attempt); retrying..." >&2
  sleep 5
  if [ "$attempt" = 3 ]; then
    echo "apt.llvm.org signing key could not be verified" >&2
    exit 1
  fi
done
echo "deb https://apt.llvm.org/noble/ llvm-toolchain-noble-19 main" | sudo tee /etc/apt/sources.list.d/llvm-toolchain.list
sudo apt-get update $APT_OPTS
sudo apt-get install -y $APT_OPTS clang-19 g++-14 llvm-19 lld-19 ffmpeg fonts-dejavu-core

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
