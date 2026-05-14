#!/usr/bin/env bash
# Build a custom Ubuntu 24.04 Server ISO with autoinstall pre-baked.
# Usage: ./build_iso.sh <input.iso> <output.iso> [extras_dir]
set -euo pipefail

IN="${1:?path to ubuntu-24.04-server amd64 .iso required}"
OUT="${2:?output iso path required}"
EXTRAS="${3:-}"

WORK=$(mktemp -d)
trap "rm -rf $WORK" EXIT

mkdir -p "$WORK/nocloud"
cp "$(dirname "$0")/autoinstall/user-data" "$WORK/nocloud/user-data"
cp "$(dirname "$0")/autoinstall/meta-data"  "$WORK/nocloud/meta-data"

HASH="${NVR_AUTOINSTALL_PASSWORD_HASH:-}"
if [[ -n "$HASH" ]]; then
  if [[ "$(uname -s)" == "Darwin" ]]; then
    sed -i '' "s|PASSWORD_HASH_PLACEHOLDER|${HASH}|g" "$WORK/nocloud/user-data"
  else
    sed -i "s|PASSWORD_HASH_PLACEHOLDER|${HASH}|g" "$WORK/nocloud/user-data"
  fi
else
  echo "note: NVR_AUTOINSTALL_PASSWORD_HASH unset; ISO still contains PASSWORD_HASH_PLACEHOLDER (invalid until you rebuild with a hash)." >&2
fi

EXTRA_ARGS=()
if [[ -n "$EXTRAS" && -d "$EXTRAS" ]]; then
  mkdir -p "$WORK/extras"
  cp -r "$EXTRAS"/* "$WORK/extras/"
  EXTRA_ARGS+=("-add" "/extras=$WORK/extras")
fi

xorriso -indev "$IN" -outdev "$OUT" \
  -boot_image any replay \
  -map "$WORK/nocloud" "/nocloud" \
  ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} \
  -volid "NVR_APPLIANCE" \
  -joliet on -rockridge on

echo "Wrote $OUT"
echo "Boot it with kernel arg: autoinstall ds=nocloud;s=/cdrom/nocloud/"
