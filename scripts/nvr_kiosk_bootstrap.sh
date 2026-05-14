#!/usr/bin/env bash
# Ensures /etc/nvr-prototype/kiosk.token exists. If missing or stale,
# requests a fresh token from the local API using credentials from the environment:
#   KIOSK_PROVISION_LOGIN and KIOSK_PROVISION_PASSWORD (both required).
# JSON: python3 only (package already depends on python3; no jq).
set -euo pipefail

TOKEN_FILE=/etc/nvr-prototype/kiosk.token
mkdir -p "$(dirname "$TOKEN_FILE")"

if [ -s "$TOKEN_FILE" ] && [ "$(find "$TOKEN_FILE" -mtime -150)" ]; then
  exit 0
fi

if [ -z "${KIOSK_PROVISION_PASSWORD:-}" ] || [ -z "${KIOSK_PROVISION_LOGIN:-}" ]; then
  exit 0
fi

resp=$(curl -fsS -X POST "${NVR_KIOSK_API_BASE:-https://nvr.local}/api/v1/auth/login" \
       -H 'Content-Type: application/json' \
       -d "{\"login\":\"${KIOSK_PROVISION_LOGIN}\",\"password\":\"$KIOSK_PROVISION_PASSWORD\"}") || exit 0
tok=$(printf '%s' "$resp" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('token') or '')")
[ -z "$tok" ] && exit 0

kiosk=$(curl -fsS -X POST "${NVR_KIOSK_API_BASE:-https://nvr.local}/api/v1/kiosk/token" \
           -H "Authorization: Bearer $tok" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('token') or '')")
[ -z "$kiosk" ] && exit 0

echo -n "$kiosk" > "$TOKEN_FILE"
chmod 640 "$TOKEN_FILE"
chown root:operator "$TOKEN_FILE" 2>/dev/null || true
