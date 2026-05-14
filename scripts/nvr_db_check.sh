#!/usr/bin/env bash
# Boot-time integrity check & best-effort recovery for the NVR SQLite database.
set -euo pipefail

DB=/var/lib/nvr-prototype/nvr.db
[ -f "$DB" ] || exit 0

result=$(sqlite3 "$DB" 'PRAGMA quick_check;' 2>&1 || true)
if [ "$result" = "ok" ]; then
  exit 0
fi

logger -t nvr-db-check "integrity_check returned: $result. Attempting recovery."

ts=$(date +%Y%m%d-%H%M%S)
cp "$DB" "/var/lib/nvr-prototype/nvr.db.broken-$ts"

tmp=$(mktemp -d)
trap "rm -rf $tmp" EXIT

if sqlite3 "$DB" ".recover" > "$tmp/dump.sql" 2>/dev/null; then
  sqlite3 "$tmp/new.db" < "$tmp/dump.sql"
  mv "$DB" "$DB.unsafe-$ts"
  mv "$tmp/new.db" "$DB"
  chown nvr:nvr "$DB" || true
  logger -t nvr-db-check "DB recovered from broken state."
else
  logger -t nvr-db-check "DB recovery failed; starting with broken DB."
fi
