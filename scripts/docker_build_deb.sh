#!/usr/bin/env bash
# Build Linux .deb inside Ubuntu 24.04 (Plan B). Run from repo root on macOS/Linux with Docker.
#
# С --platform linux/amd64 на Apple Silicon сборка идёт через QEMU: очень долго
# (часто 1–3+ часа), кажется «зависанием» — это нормально; проверка: docker exec … pgrep cc1plus
# Быстрый amd64 .deb: CI/сервер x86_64 или сборка прямо на целевой машине без Docker.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${ROOT}/docker-deb-out"
mkdir -p "${OUT}"

# linux/amd64 so the .deb matches typical x86_64 servers (Apple Silicon hosts build arm64 by default).
exec docker run --rm --platform linux/amd64 \
  -v "${ROOT}:/src:rw" \
  -v "${OUT}:/out" \
  ubuntu:24.04 \
  bash -exc '
export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config git ca-certificates curl \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  libavfilter-dev libavdevice-dev \
  libopencv-dev python3 python3-dev python3-numpy \
  libyaml-cpp-dev libsqlite3-dev libsodium-dev libcurl4-openssl-dev libssl-dev \
  nlohmann-json3-dev libspdlog-dev libsqlitecpp-dev pybind11-dev libasio-dev
rm -rf /src/docker-build
mkdir -p /src/docker-build
cd /src/docker-build
cmake /src -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build . --parallel "$(nproc)"
cpack -G DEB
shopt -s nullglob
for f in *.deb /src/docker-build/*.deb ./_CPack_Packages/*/*/*.deb; do
  if [[ -f "$f" ]]; then cp -v "$f" /out/; fi
done
ls -la /out/*.deb
'
