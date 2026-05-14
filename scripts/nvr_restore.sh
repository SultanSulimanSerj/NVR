#!/usr/bin/env bash
# scripts/nvr_restore.sh
#
# Atomic restore. The flow is:
#   1. Decrypt to a staging directory (no touch to live state yet).
#   2. PRAGMA integrity_check on the staged DB.
#   3. Stop nvr-prototype, snapshot existing files as `.rollback-<ts>`.
#   4. Atomically swap staged files into place.
#   5. Restart; on failure caller can `--rollback`.
#
# Usage:
#   nvr_restore.sh /path/to.tar.gz.gpg
#   nvr_restore.sh --rollback                 # restore the most recent rollback snapshot
set -euo pipefail

if [[ $EUID -ne 0 ]]; then echo "run as root" >&2; exit 1; fi

ROLLBACK_DIR=/var/backups/nvr-rollback

if [[ "${1-}" == "--rollback" ]]; then
    last=$(ls -1t "$ROLLBACK_DIR"/*.tar 2>/dev/null | head -n1 || true)
    [[ -n "$last" ]] || { echo "no rollback snapshot in $ROLLBACK_DIR" >&2; exit 1; }
    systemctl stop nvr-prototype.service || true
    tar -C / -xf "$last"
    chown -R nvr:nvr /var/lib/nvr-prototype || true
    systemctl start nvr-prototype.service
    echo "Rolled back from $last"
    exit 0
fi

in="${1:?path-to.tar.gz.gpg required}"
[[ -r "$in" ]] || { echo "$in not readable" >&2; exit 1; }

pass=/etc/nvr-prototype/backup.pass
[[ -r "$pass" ]] || { echo "missing $pass" >&2; exit 1; }

# 1. Decrypt + extract to a fresh staging dir.
staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
chmod 0700 "$staging"

gpg --batch --passphrase-file "$pass" -d "$in" | tar -C "$staging" -xz

# 2. Validate the staged database before we touch anything live.
if ! sqlite3 "$staging/nvr.db" 'PRAGMA integrity_check;' | grep -q '^ok$'; then
    echo "Staged DB failed integrity_check; aborting restore." >&2
    exit 2
fi

# 3. Snapshot current state.
mkdir -p "$ROLLBACK_DIR"
ts=$(date +%Y%m%d_%H%M%S)
roll="$ROLLBACK_DIR/nvr-$ts.tar"
echo "Snapshotting current state to $roll"
tar -cf "$roll" /var/lib/nvr-prototype/nvr.db /etc/nvr-prototype 2>/dev/null || true

# 4. Stop the service and swap.
systemctl stop nvr-prototype.service || true

# Move new DB into place via rename (atomic on same fs).
install -m 0640 -o nvr -g nvr "$staging/nvr.db" /var/lib/nvr-prototype/nvr.db.new
mv -f /var/lib/nvr-prototype/nvr.db.new /var/lib/nvr-prototype/nvr.db

# Etc tree: rsync delete-after so old keys are removed in lockstep with the
# new ones rather than mixed.
rsync -a --delete-after "$staging/etc-nvr/" /etc/nvr-prototype/

if [[ -d "$staging/letsencrypt" ]]; then
    rsync -a --delete-after "$staging/letsencrypt/" /etc/letsencrypt/
fi

chown -R nvr:nvr /var/lib/nvr-prototype
chown -R root:nvr /etc/nvr-prototype
find /etc/nvr-prototype -type f -exec chmod 0640 {} +
find /etc/nvr-prototype -type d -exec chmod 0750 {} +

# 5. Restart and verify.
systemctl start nvr-prototype.service
sleep 2
if ! systemctl is-active --quiet nvr-prototype.service; then
    echo "Service failed to start; run: nvr_restore.sh --rollback" >&2
    exit 3
fi

echo "Restored from $in (rollback: $roll)"
