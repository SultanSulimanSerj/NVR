#!/usr/bin/env bash
# scripts/setup_firewall.sh
#
# Включает ufw на appliance:
#   - явный default deny incoming;
#   - SSH ограничен rate-limit'ом;
#   - HTTP/HTTPS открыты для веб-интерфейса;
#   - 8080/tcp (внутренний бэкенд) — НЕ открываем наружу, он живёт на 127.0.0.1.
#
# Запуск:  sudo ./scripts/setup_firewall.sh [--allow-rtsp]
#   --allow-rtsp  открыть 554/tcp в обе стороны (если NVR раздаёт RTSP).
set -euo pipefail

ALLOW_RTSP=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --allow-rtsp) ALLOW_RTSP=1; shift ;;
        -h|--help)    sed -n '1,15p' "$0"; exit 0 ;;
        *)            echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ $EUID -ne 0 ]]; then echo "run as root" >&2; exit 1; fi

apt-get install -y --no-install-recommends ufw

ufw --force reset
ufw default deny incoming
ufw default allow outgoing

# SSH с rate-limit (3 connection attempts / 30 s).
ufw limit 22/tcp     comment 'ssh-admin'
ufw allow  80/tcp    comment 'nvr web (redirect)'
ufw allow 443/tcp    comment 'nvr web ssl'

# WS-Discovery multicast (3702/udp) — outgoing уже allowed, но входящих ответов
# нет на loopback-only бэкенде. Если адаптер видит broadcast — открываем явно.
ufw allow proto udp from any to any port 3702 comment 'onvif ws-discovery'

if [[ $ALLOW_RTSP -eq 1 ]]; then
    ufw allow 554/tcp  comment 'nvr rtsp passthrough'
fi

# /metrics жёстко закрыт снаружи (nginx allow loopback + ufw default deny).
ufw --force enable
ufw status verbose
