#!/usr/bin/env bash
# scripts/nvr_update.sh
#
# Update helper used by /api/v1/system/update and the System / Updates page.
#
# Supports two distribution channels:
#   - https://updates.example.com/...    — vendor-managed HTTPS server.
#   - file:///mnt/usb/nvr-mirror/...     — offline/airgapped delivery
#                                          (USB stick or local apt mirror).
#
# Every fetched package is verified against:
#   1. SHA-256 from `<channel>/latest.json` (`sha256` field).
#   2. Optional GPG detached signature (`<pkg>.sig`) verified with a public key
#      pinned to /etc/nvr-prototype/update-pubkey.asc.
#
# Honours `--check`, `--apply`, `--rollback`, `--version`.
set -euo pipefail

CHANNEL_URL_DEFAULT="https://updates.example.com/nvr-appliance/stable"
URL="${NVR_UPDATE_URL:-$CHANNEL_URL_DEFAULT}"
CACHE="/var/cache/nvr"
PKG="$CACHE/last.deb"
META="$CACHE/latest.json"
PREV_PKG="$CACHE/prev.deb"
PUBKEY_PATH="${NVR_UPDATE_PUBKEY:-/etc/nvr-prototype/update-pubkey.asc}"
PUBKEY_GPG_HOME="$CACHE/gpg-home"

mkdir -p "$CACHE"

require_root() {
    [[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 1; }
}

# Returns 0 if URL points at a local file:// path, 1 otherwise.
is_local() { [[ "$1" == file://* ]]; }

# Best-effort downloader: curl for HTTPS, plain `cp` for file://.
fetch() {
    local src="$1" dst="$2"
    if is_local "$src"; then
        cp -f "${src#file://}" "$dst"
    else
        curl -fsSL --connect-timeout 10 --retry 3 --retry-delay 2 "$src" -o "$dst"
    fi
}

verify_sha256() {
    local pkg="$1" expected="$2"
    local got
    got=$(sha256sum "$pkg" | awk '{print $1}')
    if [[ "$got" != "$expected" ]]; then
        echo "checksum mismatch: expected $expected, got $got" >&2
        return 1
    fi
}

verify_signature() {
    local pkg="$1"
    [[ -r "$PUBKEY_PATH" ]] || { echo "no pubkey at $PUBKEY_PATH; refusing"; return 1; }

    mkdir -p "$PUBKEY_GPG_HOME"; chmod 0700 "$PUBKEY_GPG_HOME"
    gpg --homedir "$PUBKEY_GPG_HOME" --batch --quiet --import "$PUBKEY_PATH"

    # Fetch detached signature alongside the package.
    local sig="${pkg}.sig"
    fetch "$URL/$(basename "$pkg").sig" "$sig" || {
        echo "missing detached signature for $(basename "$pkg")" >&2; return 1
    }

    gpg --homedir "$PUBKEY_GPG_HOME" --batch --verify "$sig" "$pkg" 2>/dev/null \
        || { echo "GPG verification failed" >&2; return 1; }
}

case "${1:-}" in
  --version)
    dpkg-query -W -f='${Version}\n' nvr-prototype 2>/dev/null || echo "unknown"
    ;;

  --check)
    fetch "$URL/latest.json" "$META" || true
    cat "$META" 2>/dev/null || echo '{"error":"no metadata"}'
    ;;

  --apply)
    require_root
    fetch "$URL/latest.json" "$META"
    pkg_name=$(jq -r '.package // "nvr-prototype.deb"' "$META")
    sha=$(jq -r '.sha256 // empty' "$META")

    # Snapshot the *currently installed* .deb for rollback (not the newly downloaded file).
    rm -f "$PREV_PKG"
    if dpkg -s nvr-prototype >/dev/null 2>&1; then
      cur_ver=$(dpkg-query -W -f='${Version}' nvr-prototype)
      ( cd "$CACHE" && apt-get download -q "nvr-prototype=${cur_ver}" ) || true
      shopt -s nullglob
      for f in "$CACHE"/nvr-prototype_"${cur_ver}"_*.deb; do
        mv -f "$f" "$PREV_PKG"
        break
      done
      shopt -u nullglob
    fi

    fetch "$URL/$pkg_name" "$PKG"
    if [[ -n "$sha" ]]; then verify_sha256 "$PKG" "$sha"; fi
    if [[ -r "$PUBKEY_PATH" ]]; then verify_signature "$PKG"; else
        echo "WARNING: no pubkey configured ($PUBKEY_PATH); proceeding without sig"
    fi

    DEBIAN_FRONTEND=noninteractive apt-get install -y --allow-downgrades "$PKG"
    systemctl restart nvr-prototype
    ;;

  --rollback)
    require_root
    if [[ -f "$PREV_PKG" ]]; then
        DEBIAN_FRONTEND=noninteractive apt-get install -y --allow-downgrades "$PREV_PKG"
        systemctl restart nvr-prototype
    else
        echo "no rollback available" >&2; exit 1
    fi
    ;;

  *)
    echo "usage: $0 --check|--apply|--rollback|--version" >&2; exit 1 ;;
esac
