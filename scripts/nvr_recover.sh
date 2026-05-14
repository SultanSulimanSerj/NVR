#!/usr/bin/env bash
# Recovery shell for the NVR appliance.
#
# Break-glass: intended only from a trusted local recovery console (GRUB recovery,
# init=/bin/bash, or single-user). Do not expose this workflow over the network.
#
# This is loaded from the GRUB recovery entry. It is expected to run as root
# (the boot path drops us into a `init=/bin/bash` shell). We still EUID-check
# to be friendly when the operator runs it manually.
#
# Actions:
#   1) DB integrity check (SQLite PRAGMA + .recover fallback)
#   2) Reset admin password (re-trigger the first-run flow)
#   3) Tail service log
#   4) Reset network (DHCP)
#   5) Encrypted backup of config (uses GPG passphrase from backup.pass)
#   6) Restart services
#   7) Print master.key SHA256 (read-only diagnostic, key never printed)
set -euo pipefail

if [[ $EUID -ne 0 ]]; then echo "run as root (e.g. sudo $0)" >&2; exit 1; fi

DB=/var/lib/nvr-prototype/nvr.db
CFG=/etc/nvr-prototype/nvr.yaml
BACKUP_PASS=/etc/nvr-prototype/backup.pass

dbcheck() {
    if [[ ! -f "$DB" ]]; then
        echo "DB missing at $DB"; return 1
    fi
    sqlite3 "$DB" 'PRAGMA quick_check;'
    sqlite3 "$DB" 'PRAGMA integrity_check;'
    sqlite3 "$DB" 'PRAGMA foreign_key_check;'
}

reset_admin() {
    echo "Clearing admin user; the daemon will regenerate one on next start"
    echo "and write the new password to /etc/nvr-prototype/initial-admin.password"
    sqlite3 "$DB" "DELETE FROM users WHERE login='admin';"
    sqlite3 "$DB" "DELETE FROM refresh_tokens;"
    sqlite3 "$DB" "DELETE FROM token_revocations;"
    rm -f /var/lib/nvr-prototype/.setup-done
    echo "Restart nvr-prototype to generate a fresh admin password."
}

reset_network() {
    cat > /etc/netplan/99-nvr.yaml <<'EOF'
network:
  version: 2
  ethernets:
    en:
      match: { name: "en*" }
      dhcp4: true
EOF
    chmod 0600 /etc/netplan/99-nvr.yaml
    netplan apply
}

backup_now() {
    if [[ ! -r "$BACKUP_PASS" ]]; then
        echo "$BACKUP_PASS missing (root 0600). Generating a one-shot key…"
        install -m 0600 -o root -g root /dev/stdin "$BACKUP_PASS" \
            < <(head -c 32 /dev/urandom | base64)
    fi
    out=/root/nvr-recover-$(date +%Y%m%d-%H%M%S).tar.gz.gpg
    tar -czf - /etc/nvr-prototype /var/lib/nvr-prototype/nvr.db \
        | gpg --symmetric --cipher-algo AES256 --batch \
              --passphrase-file "$BACKUP_PASS" -o "$out"
    echo "Encrypted backup saved to $out (passphrase: $BACKUP_PASS)"
}

masterkey_fingerprint() {
    if [[ -r /etc/nvr-prototype/master.key ]]; then
        sha256sum /etc/nvr-prototype/master.key
    else
        echo "master.key missing — restore from backup or run setup wizard"
    fi
}

menu() {
    cat <<'EOF'

=== NVR Recovery ===
1) DB integrity check
2) Reset admin user (forces password regeneration on next boot)
3) Tail service log (last 200 lines)
4) Reset network configuration (DHCP)
5) Encrypted backup of config + DB (gpg, AES-256)
6) Restart services
7) Print master.key SHA-256 (diagnostic only)
0) Exit
EOF
    read -r -p "choice: " a
    case "$a" in
        1) dbcheck ;;
        2) reset_admin ;;
        3) journalctl -u nvr-prototype -n 200 --no-pager ;;
        4) reset_network ;;
        5) backup_now ;;
        6) systemctl restart nvr-prototype nvr-kiosk 2>/dev/null || true ;;
        7) masterkey_fingerprint ;;
        0) exit 0 ;;
    esac
}

while true; do menu; done
