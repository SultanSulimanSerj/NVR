# NVR Appliance — Security Posture

## Layers of defence

| Layer | What it does | Where it lives |
|-------|--------------|----------------|
| systemd hardening | `NoNewPrivileges`, `ProtectSystem`, `CapabilityBoundingSet`, `SystemCallFilter`, `Type=notify` watchdog | [`deploy/nvr-prototype.service`](../deploy/nvr-prototype.service) |
| AppArmor | Enforce-mode profile pinning the daemon to its working dirs and a tiny allowlist of subprocesses | [`deploy/apparmor/usr.bin.nvr_prototype`](../deploy/apparmor/usr.bin.nvr_prototype) |
| nginx reverse-proxy | TLS termination, HSTS, CSP, rate limits per zone (`nvr_api`, `nvr_auth`, `nvr_hls`), `/metrics` allow loopback | [`deploy/nginx/nvr.conf`](../deploy/nginx/nvr.conf) |
| ufw | Default deny incoming, `ssh limit`, only 80/443 open externally | [`scripts/setup_firewall.sh`](../scripts/setup_firewall.sh) |
| App-level | RBAC (Admin / Operator / Viewer), JWT access+refresh with `token_version` revocation, TOTP encrypted at rest, audit log | `src/api/Auth.cpp`, `src/api/HttpServer.cpp` |
| Storage | ChaCha20-Poly1305 with `libsodium`, master key in `/etc/nvr-prototype/master.key` `0640 root:nvr` | `src/store/Crypto.cpp` |

## Enabling AppArmor in enforce mode

```bash
sudo cp deploy/apparmor/usr.bin.nvr_prototype /etc/apparmor.d/
sudo apparmor_parser -r /etc/apparmor.d/usr.bin.nvr_prototype
sudo aa-enforce /etc/apparmor.d/usr.bin.nvr_prototype
sudo systemctl restart nvr-prototype
```

Verify with `aa-status` — the profile must appear under "enforce".

If a future release adds a new helper binary or path, edit the profile and
re-parse. Don't switch to complain mode in production unless you're debugging.

## Firewall checklist

`scripts/setup_firewall.sh` ships a sensible default. The internal daemon
listens on `127.0.0.1:8080` only, so nothing besides `nginx` reaches it.
If you expose Crow directly (no nginx), open `8080/tcp` and set `bind_address`
in `nvr.yaml` to `0.0.0.0` — but you lose HSTS/CSP headers added by nginx.

## Reset / break-glass

Break-glass assumes **physical or out-of-band console access** to the appliance (recovery shell / single-user), not remote exploitation of the API.

1. Boot the recovery entry from GRUB (or `init=/bin/bash`) and run [`scripts/nvr_recover.sh`](../scripts/nvr_recover.sh) as **root**.
2. Use menu options there to: verify SQLite (`PRAGMA quick_check`), optionally reset the `admin` user + remove `.setup-done`, tail logs, or re-apply DHCP netplan.
3. Encrypted config backups (`nvr_backup.sh`) decrypt with `/etc/nvr-prototype/backup.pass` — keep that file **0600** and rotate with `nvr_backup.sh --rotate-pass` when policy requires.
4. After deleting `/var/lib/nvr-prototype/.setup-done`, the web **Setup** flow becomes available again when a new `/etc/nvr-prototype/setup.token` exists (`0640 root:nvr`).

## What's NOT defended yet (known limitations)

- RTSP credentials are stored encrypted, but anyone with root on the box can
  read them — same trust boundary as the master key.
- The kiosk shell (Chromium or `nvr-shell`) trusts the local cert; in a hostile
  LAN you should add a corporate CA and re-issue.
- AppArmor's `network` rules currently allow all outbound TCP/UDP. Tighten via
  egress firewalls if your deployment requires it.
