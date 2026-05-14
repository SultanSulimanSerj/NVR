#!/usr/bin/env bash
# Optional archive compressor: re-encodes segments older than N days using QSV
# or CPU at a lower bitrate to reclaim disk space.
set -euo pipefail

ARCHIVE_ROOT=${NVR_ARCHIVE_ROOT:-/var/lib/nvr-prototype/archive}
DAYS=${NVR_COMPRESS_OLDER_THAN_DAYS:-14}
BITRATE=${NVR_COMPRESS_BITRATE:-1000k}
HW=${NVR_COMPRESS_HW:-auto}

find "$ARCHIVE_ROOT" -type f -name '*.mp4' -mtime "+$DAYS" -print0 |
  while IFS= read -r -d '' f; do
    out="${f%.mp4}.lq.mp4"
    [ -f "$out" ] && continue
    case "$HW" in
      qsv) codec="-c:v h264_qsv -b:v $BITRATE" ;;
      vaapi) codec="-vaapi_device /dev/dri/renderD128 -vf format=nv12,hwupload -c:v h264_vaapi -b:v $BITRATE" ;;
      *)    codec="-c:v libx264 -b:v $BITRATE -preset veryfast" ;;
    esac
    if nice -n 19 ionice -c2 -n7 ffmpeg -loglevel error -y -i "$f" $codec -c:a copy "$out"; then
      mv "$out" "$f"
    else
      rm -f "$out"
    fi
  done
