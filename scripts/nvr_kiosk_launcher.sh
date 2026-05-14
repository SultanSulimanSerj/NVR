#!/usr/bin/env bash
# Starts cage with either native nvr-shell (if installed) or Chromium kiosk flags.
set -euo pipefail

TOKEN="$(cat /etc/nvr-prototype/kiosk.token 2>/dev/null || true)"
BASE="${NVR_KIOSK_BASE_URL:-https://nvr.local}"
URL="${BASE}/local-dashboard"
if [[ -n "${TOKEN}" ]]; then
  URL="${URL}?kiosk_token=${TOKEN}"
fi

CHROME=""
for c in /usr/bin/chromium /usr/bin/chromium-browser; do
  if [[ -x "$c" ]]; then CHROME="$c"; break; fi
done

KIOSK_CLASS="${NVR_KIOSK_WM_CLASS:-NVRMosaic}"

NVR_SHELL=""
for s in /usr/bin/nvr-shell /usr/local/bin/nvr-shell; do
  if [[ -x "$s" ]]; then NVR_SHELL="$s"; break; fi
done

if [[ -n "$NVR_SHELL" ]]; then
  export NVR_KIOSK_URL="${URL}"
  exec /usr/bin/cage -s -- "$NVR_SHELL"
fi

if [[ -z "$CHROME" ]]; then
  echo "nvr_kiosk_launcher: neither nvr-shell nor chromium found" >&2
  exit 1
fi

exec /usr/bin/cage -s -- "$CHROME" \
  --kiosk \
  --noerrdialogs \
  --disable-translate \
  --disable-infobars \
  --disable-session-crashed-bubble \
  --disable-restore-session-state \
  --no-default-browser-check \
  --disable-breakpad \
  --disable-features=TranslateUI,InterestFeedContentSuggestions,OptimizationHints \
  --autoplay-policy=no-user-gesture-required \
  --check-for-update-interval=31536000 \
  --password-store=basic \
  --no-first-run \
  --start-fullscreen \
  --enable-features=UseOzonePlatform \
  --ozone-platform=wayland \
  --class="${KIOSK_CLASS}" \
  --name="${KIOSK_CLASS}" \
  --app="${URL}"
