#!/usr/bin/env bash
# Сторож при сборке на сервере: запускайте во втором SSH-пока идёт setup_ubuntu_nvr.sh.
# Каждые N секунд печатает число ninja/cc/git-http — если долго ninja=1, cc=0, git_http>0,
# часто это клон FetchContent с GitHub (сеть), а не «мёртвый» процесс.
set -u
INTERVAL="${1:-45}"
echo "[watch] интервал ${INTERVAL}s; Ctrl+C чтобы выйти (setup не останавливается)"
while true; do
  ts=$(date -Is)
  if ! pgrep -x ninja >/dev/null 2>&1 && ! pgrep -f setup_ubuntu_nvr.sh >/dev/null 2>&1; then
    echo "[watch] $ts нет ninja и setup_ubuntu_nvr.sh — выходим (сборка закончилась или не запущена)"
    break
  fi
  nn=$(pgrep -c -x ninja 2>/dev/null || echo 0)
  cc=$(pgrep -c -E 'cc1plus|clang\+\+' 2>/dev/null || echo 0)
  gh=$(pgrep -c git-remote-http 2>/dev/null || echo 0)
  cm=$(pgrep -c -E 'cmake.*populate' 2>/dev/null || echo 0)
  echo "[watch] $ts ninja=$nn cc1plus_like=$cc git_remote_http=$gh cmake_populate=$cm"
  sleep "$INTERVAL"
done
