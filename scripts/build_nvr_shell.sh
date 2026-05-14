#!/usr/bin/env bash
# Сборка нативного клиента киоска (Rust + WebKitGTK). Нужны: cargo, libgtk-3-dev, libwebkit2gtk-4.1-dev.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}/nvr-shell"
cargo build --release
echo "Binary: ${ROOT}/nvr-shell/target/release/nvr-shell"
