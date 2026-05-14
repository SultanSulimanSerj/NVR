#!/usr/bin/env bash
# Обновить исходники с GitHub на сервере (clone или pull).
# Переменные: NVR_GIT_URL (по умолчанию репозиторий проекта), NVR_CLONE_DIR (по умолчанию ~/nvr-prototype).
set -euo pipefail
URL="${NVR_GIT_URL:-https://github.com/SultanSulimanSerj/NVR.git}"
DEST="${NVR_CLONE_DIR:-$HOME/nvr-prototype}"

if [[ -d "${DEST}/.git" ]]; then
  echo "[sync] git -C ${DEST} pull"
  git -C "${DEST}" remote get-url origin >/dev/null 2>&1 || git -C "${DEST}" remote add origin "${URL}" || true
  git -C "${DEST}" fetch origin
  git -C "${DEST}" checkout -B main 2>/dev/null || git -C "${DEST}" checkout main
  git -C "${DEST}" pull --ff-only origin main || git -C "${DEST}" pull origin main
else
  echo "[sync] git clone ${URL} -> ${DEST}"
  git clone "${URL}" "${DEST}"
fi

echo "[sync] готово."
echo "Дальше (один раз): sudo apt-get install -y nginx"
echo "Затем:  cd ${DEST} && sudo ./scripts/setup_ubuntu_nvr.sh"
echo "Во втором SSH параллельно:  ${DEST}/scripts/build_hang_watch.sh"
