#!/usr/bin/env bash
# scripts/nvr_backup.sh
#
# Resilient backup of the NVR appliance config + secrets:
#   1. SQLite online backup (no WAL contention).
#   2. /etc/nvr-prototype (config.yaml, master.key, jwt.secret, branding.json,
#      tls/*).
#   3. /etc/letsencrypt — best-effort if present.
#
# Output is an AES256-encrypted tarball signed-by-passphrase via gpg. The
# passphrase lives in /etc/nvr-prototype/backup.pass (root 0600).
#
# Usage:
#   nvr_backup.sh                            # default /var/backups/nvr-<ts>.tar.gz.gpg
#   nvr_backup.sh /path/to/out.tar.gz.gpg    # explicit path
#   nvr_backup.sh --rsync user@host:/path    # also rsync to off-host
#   nvr_backup.sh --age PUBKEY               # encrypt with age in addition to gpg
#   nvr_backup.sh --rotate-pass              # regenerate backup.pass (old .gpg files need old passphrase)
#
# The script never deletes prior backups; rotate via cron or external policy.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then echo "run as root" >&2; exit 1; fi

out=""
rsync_dst=""
age_pub=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rsync) rsync_dst="$2"; shift 2 ;;
        --age)   age_pub="$2";   shift 2 ;;
        --rotate-pass)
            head -c 32 /dev/urandom | base64 > /etc/nvr-prototype/backup.pass.new
            chmod 0600 /etc/nvr-prototype/backup.pass.new
            mv -f /etc/nvr-prototype/backup.pass.new /etc/nvr-prototype/backup.pass
            echo "Rotated /etc/nvr-prototype/backup.pass (old backups still need the old passphrase)." >&2
            shift
            ;;
        -h|--help) sed -n '1,30p' "$0"; exit 0 ;;
        *)       out="$1"; shift ;;
    esac
done

out="${out:-/var/backups/nvr-$(date +%Y%m%d_%H%M%S).tar.gz.gpg}"
mkdir -p "$(dirname "$out")"

pass=/etc/nvr-prototype/backup.pass
[[ -r "$pass" ]] || { echo "missing $pass (run setup_ubuntu_nvr.sh)" >&2; exit 1; }
chmod 0600 "$pass"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
chmod 0700 "$tmp"

# 1. SQLite online-copy (works even while nvr_prototype is running).
sqlite3 /var/lib/nvr-prototype/nvr.db ".backup '$tmp/nvr.db'"
sqlite3 "$tmp/nvr.db" 'PRAGMA integrity_check;' | grep -q '^ok$' \
    || { echo "DB backup failed integrity_check" >&2; exit 2; }

cp -a /etc/nvr-prototype "$tmp/etc-nvr"
[[ -d /etc/letsencrypt ]] && cp -a /etc/letsencrypt "$tmp/letsencrypt" || true

# 2. tar + gpg AES256.
tar -C "$tmp" -czf - . | gpg --symmetric --cipher-algo AES256 --batch \
    --passphrase-file "$pass" -o "$out"

# 3. Optional age-encrypted side copy for off-host upload (gpg alone is fine
# but customers occasionally want both).
if [[ -n "$age_pub" ]] && command -v age >/dev/null; then
    age -r "$age_pub" -o "${out%.gpg}.age" -- <(tar -C "$tmp" -cz . ) || true
fi

# 4. Off-host rsync over SSH if requested.
if [[ -n "$rsync_dst" ]]; then
    rsync -avz --partial -e 'ssh -o StrictHostKeyChecking=accept-new' \
        "$out" "$rsync_dst/" || echo "rsync to $rsync_dst failed (continuing)"
fi

echo "Backup written to $out"
