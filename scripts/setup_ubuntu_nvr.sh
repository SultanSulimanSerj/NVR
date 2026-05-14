#!/usr/bin/env bash
# scripts/setup_ubuntu_nvr.sh
#
# Подготавливает Ubuntu Server (22.04 / 24.04) под прототип NVR:
#   1) Устанавливает dev-пакеты для сборки (FFmpeg/OpenCV/pybind11/Python/CMake/Ninja).
#   2) Ставит стек Intel Media (QSV/VAAPI) + vainfo для проверки.
#   3) Создаёт системного пользователя nvr и каталоги архива/снимков/логов.
#   4) Собирает и устанавливает бинарь.
#   5) Применяет sysctl-тюнинг сети/диска.
#   6) Включает systemd-юнит и kiosk-режим (tty1 + cage, резервная tty2, Magic-SysRq, тихий GRUB).
#
# Использование:
#   sudo ./scripts/setup_ubuntu_nvr.sh [--kiosk] [--build-dir <dir>]
#
# Запуск без --kiosk оставит обычный сервер без kiosk-ограничений.

set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
KIOSK=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --kiosk)     KIOSK=1; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help)
            sed -n '1,30p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 2
            ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "Этот скрипт нужно запускать с правами root (sudo)." >&2
    exit 1
fi

log() { printf '\033[1;36m[setup]\033[0m %s\n' "$*"; }

# ---------------------------------------------------------------------------
# 1. Пакеты сборки и рантайма
# ---------------------------------------------------------------------------
log "apt update + установка пакетов"
export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git ca-certificates curl \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
    libavfilter-dev libavdevice-dev \
    libopencv-dev \
    python3 python3-dev python3-numpy pybind11-dev \
    libyaml-cpp-dev libsqlite3-dev libsodium-dev libcurl4-openssl-dev nlohmann-json3-dev \
    libspdlog-dev libsqlitecpp-dev \
    libssl-dev \
    intel-media-va-driver-non-free vainfo i965-va-driver \
    libva-drm2 libva-x11-2 \
    smartmontools chrony unattended-upgrades \
    sudo systemd

# Intel Media SDK userland (optional; names differ across Ubuntu releases).
apt-get install -y --no-install-recommends libmfx1 2>/dev/null || \
apt-get install -y --no-install-recommends libmfx-gen1.2 2>/dev/null || \
log "пакет libmfx не найден в apt (пропуск, VAAPI/QSV через другие библиотеки)"

# ---------------------------------------------------------------------------
# 2. Пользователь, директории, права
# ---------------------------------------------------------------------------
log "создаём пользователя nvr и каталоги"
if ! id -u nvr >/dev/null 2>&1; then
    useradd --system --create-home --home-dir /var/lib/nvr-prototype \
            --shell /usr/sbin/nologin --user-group nvr
fi
usermod -aG video,render nvr || true

install -d -m 0750 -o nvr -g nvr /var/lib/nvr-prototype
install -d -m 0750 -o nvr -g nvr /var/lib/nvr-prototype/archive
install -d -m 0750 -o nvr -g nvr /var/lib/nvr-prototype/snapshots
install -d -m 0755 -o root -g root /etc/nvr-prototype
install -d -m 0755 -o root -g root /usr/share/nvr-prototype/scripts
install -d -m 0750 -o nvr -g nvr /var/log/nvr-prototype

# ---------------------------------------------------------------------------
# 3. Сборка проекта
# ---------------------------------------------------------------------------
log "сборка проекта в ${BUILD_DIR}"
# Install prefix MUST be /usr — service units reference /usr/bin/nvr_prototype
# and the .deb places everything under /usr, so a source build needs to match.
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"
cmake --install "${BUILD_DIR}"

install -m 0644 "${PROJECT_ROOT}/config/nvr.yaml" /etc/nvr-prototype/nvr.yaml
install -m 0644 "${PROJECT_ROOT}/scripts/script.py" /usr/share/nvr-prototype/scripts/script.py
install -m 0644 "${PROJECT_ROOT}/deploy/nvr-prototype.service" /etc/systemd/system/nvr-prototype.service

if [[ -f "${PROJECT_ROOT}/deploy/nvr-kiosk.service" ]]; then
    install -m 0644 "${PROJECT_ROOT}/deploy/nvr-kiosk.service" /etc/systemd/system/nvr-kiosk.service
fi
install -m 0644 "${PROJECT_ROOT}/deploy/50-unattended-upgrades.conf" \
        /etc/apt/apt.conf.d/50-unattended-upgrades-nvr || true
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_backup.sh"  /usr/bin/nvr-backup
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_restore.sh" /usr/bin/nvr-restore
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_recover.sh" /usr/bin/nvr-recover
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_update.sh"  /usr/bin/nvr-update
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_db_check.sh"          /usr/bin/nvr_db_check.sh
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_archive_compress.sh"  /usr/bin/nvr_archive_compress.sh
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_kiosk_bootstrap.sh" \
    /usr/share/nvr-prototype/scripts/nvr_kiosk_bootstrap.sh
install -m 0755 "${PROJECT_ROOT}/scripts/nvr_kiosk_launcher.sh" \
    /usr/share/nvr-prototype/scripts/nvr_kiosk_launcher.sh

install -m 0644 "${PROJECT_ROOT}/deploy/nvr-db-check.service"        /etc/systemd/system/nvr-db-check.service
install -m 0644 "${PROJECT_ROOT}/deploy/nvr-archive-compress.service" /etc/systemd/system/nvr-archive-compress.service
install -m 0644 "${PROJECT_ROOT}/deploy/nvr-archive-compress.timer"   /etc/systemd/system/nvr-archive-compress.timer
install -m 0644 "${PROJECT_ROOT}/deploy/branding.json" /etc/nvr-prototype/branding.json
install -m 0644 "${PROJECT_ROOT}/deploy/nginx/nvr.conf"  /etc/nginx/sites-available/nvr.conf 2>/dev/null || true
install -d -m 0755 /etc/nginx/snippets 2>/dev/null || true
install -m 0644 "${PROJECT_ROOT}/deploy/nginx/snippets/nvr-ssl-letsencrypt.conf" \
        /etc/nginx/snippets/nvr-ssl-letsencrypt.conf 2>/dev/null || true
install -m 0644 "${PROJECT_ROOT}/deploy/nginx/snippets/nvr-ssl-self-signed.conf" \
        /etc/nginx/snippets/nvr-ssl-self-signed.conf 2>/dev/null || true
# First-boot default: self-signed snippet. Operator switches to letsencrypt by
# pointing /etc/nginx/snippets/nvr-ssl.conf at the LE file once cert is issued.
if [[ ! -e /etc/nginx/snippets/nvr-ssl.conf ]]; then
    ln -sf /etc/nginx/snippets/nvr-ssl-self-signed.conf /etc/nginx/snippets/nvr-ssl.conf || true
fi
if [[ -d /etc/nginx/sites-enabled ]] && [[ ! -e /etc/nginx/sites-enabled/nvr.conf ]]; then
    ln -sf /etc/nginx/sites-available/nvr.conf /etc/nginx/sites-enabled/nvr.conf || true
fi
install -m 0755 "${PROJECT_ROOT}/scripts/setup_firewall.sh" /usr/sbin/nvr-firewall-setup

if [[ ! -f /etc/nvr-prototype/backup.pass ]]; then
    head -c 32 /dev/urandom | base64 > /etc/nvr-prototype/backup.pass
    chmod 600 /etc/nvr-prototype/backup.pass
fi

# One-shot token for first-run wizard (readable by nvr daemon: group nvr, 0640).
if [[ ! -f /etc/nvr-prototype/setup.token ]]; then
    head -c 32 /dev/urandom | base64 | tr -d '\n=+/' > /etc/nvr-prototype/setup.token
    chown root:nvr /etc/nvr-prototype/setup.token
    chmod 0640 /etc/nvr-prototype/setup.token
    log "создан /etc/nvr-prototype/setup.token (покажите оператору для мастера настройки)"
fi

if [[ ! -f /etc/nvr-prototype/tls/cert.pem ]]; then
    install -d -m 0750 /etc/nvr-prototype/tls
    openssl req -x509 -newkey rsa:4096 -sha256 -days 730 -nodes \
        -keyout /etc/nvr-prototype/tls/key.pem \
        -out    /etc/nvr-prototype/tls/cert.pem \
        -subj "/C=US/ST=NA/L=NA/O=NVR/CN=$(hostname -f 2>/dev/null || hostname)" \
        -addext "subjectAltName=DNS:$(hostname),DNS:localhost,DNS:nvr.local,IP:127.0.0.1"
    chmod 600 /etc/nvr-prototype/tls/key.pem
fi

# Appliance hostname for kiosk / TLS (avoid «localhost» in client URL).
if [[ -f /etc/hosts ]] && ! grep -qE '^127\.0\.0\.1[[:space:]]+nvr\.local(\s|$)' /etc/hosts; then
    echo '127.0.0.1 nvr.local' >>/etc/hosts
fi
if [[ -f /etc/nvr-prototype/tls/cert.pem ]]; then
    install -m 0644 /etc/nvr-prototype/tls/cert.pem \
        /usr/local/share/ca-certificates/nvr-prototype.crt 2>/dev/null || true
    update-ca-certificates >/dev/null 2>&1 || true
fi

chown -R nvr:nvr /var/lib/nvr-prototype /var/log/nvr-prototype

# ---------------------------------------------------------------------------
# 4. sysctl
# ---------------------------------------------------------------------------
log "применяем sysctl-тюнинг"
install -m 0644 "${PROJECT_ROOT}/deploy/nvr-prototype.sysctl.conf" \
        /etc/sysctl.d/99-nvr-prototype.conf
sysctl --system >/dev/null

# ---------------------------------------------------------------------------
# 5. Лимиты и журналирование
# ---------------------------------------------------------------------------
log "настраиваем journald (persistent) и лимиты"
install -d -m 0755 /var/log/journal
systemd-tmpfiles --create --prefix /var/log/journal || true
mkdir -p /etc/systemd/journald.conf.d
cat >/etc/systemd/journald.conf.d/00-nvr.conf <<'EOF'
[Journal]
Storage=persistent
SystemMaxUse=2G
SystemKeepFree=1G
RuntimeMaxUse=200M
ForwardToSyslog=no
EOF
systemctl restart systemd-journald

cat >/etc/security/limits.d/nvr.conf <<'EOF'
nvr  soft  nofile  131072
nvr  hard  nofile  131072
nvr  soft  nproc   8192
nvr  hard  nproc   8192
EOF

# ---------------------------------------------------------------------------
# 6. systemd
# ---------------------------------------------------------------------------
log "включаем сервис nvr-prototype.service"
systemctl daemon-reload
systemctl enable nvr-prototype.service
systemctl restart nvr-prototype.service || {
    log "сервис не стартовал, см. journalctl -u nvr-prototype"
}

# ---------------------------------------------------------------------------
# 7. Kiosk Mode (опционально)
# ---------------------------------------------------------------------------
if [[ ${KIOSK} -eq 1 ]]; then
    log "включаем kiosk-режим ОС"
    apt-get install -y --no-install-recommends \
        cage chromium-browser seatd xkb-data fonts-noto-color-emoji \
        libwebkit2gtk-4.1-0 libgtk-3-0
    systemctl enable seatd.service || true
    systemctl enable nvr-kiosk.service || true

    # tty1: nvr-kiosk (cage + UI). tty2: оставляем getty для аварийной консоли (Ctrl+Alt+F2).
    # tty3–tty6: маскируем, чтобы клиент не видел лишних Linux-консолей. Вернуться к киоску: Ctrl+Alt+F1.
    systemctl unmask getty@tty2.service 2>/dev/null || true
    systemctl mask getty@tty1.service getty@tty3.service getty@tty4.service \
                   getty@tty5.service getty@tty6.service || true
    systemctl disable getty@tty1.service getty@tty3.service getty@tty4.service \
                       getty@tty5.service getty@tty6.service 2>/dev/null || true
    rm -rf /etc/systemd/system/getty@tty1.service.d

    systemctl daemon-reload || true
    systemctl enable getty@tty2.service || true
    systemctl start getty@tty2.service 2>/dev/null || true

    # Пользователь для DRM/Wayland (без интерактивного логина на tty).
    if ! id -u operator >/dev/null 2>&1; then
        useradd --create-home --shell /usr/sbin/nologin operator
        echo "operator:operator" | chpasswd
        passwd -e operator >/dev/null
    else
        usermod -s /usr/sbin/nologin operator 2>/dev/null || true
    fi
    usermod -aG video,render operator 2>/dev/null || true

    # Запрет Magic SysRq, Ctrl+Alt+Del (опционально оставляем reboot).
    cat >/etc/sysctl.d/98-nvr-kiosk.conf <<'EOF'
kernel.sysrq          = 0
kernel.dmesg_restrict = 1
kernel.kptr_restrict  = 2
EOF
    sysctl --system >/dev/null

    # logind: не отключаем переключение VT — нужен Ctrl+Alt+F2 на tty2.
    install -d -m 0755 /etc/X11
    cat >/etc/systemd/logind.conf.d/00-nvr-kiosk.conf 2>/dev/null <<'EOF' || true
[Login]
KillUserProcesses=yes
HandleCtrlAltDel=reboot
EOF

    # Запрет sudo для operator. Сам пользователь nvr — system, без shell.
    if [[ -f /etc/sudoers ]]; then
        if ! grep -q '^operator' /etc/sudoers.d/00-nvr-kiosk 2>/dev/null; then
            cat >/etc/sudoers.d/00-nvr-kiosk <<'EOF'
operator ALL=(ALL) !ALL
EOF
            chmod 0440 /etc/sudoers.d/00-nvr-kiosk
        fi
    fi

    # Загрузчик: тихий boot, без меню.
    if [[ -f /etc/default/grub ]]; then
        sed -i 's/^GRUB_TIMEOUT=.*/GRUB_TIMEOUT=0/' /etc/default/grub
        sed -i 's/^GRUB_TIMEOUT_STYLE=.*/GRUB_TIMEOUT_STYLE=hidden/' /etc/default/grub
        sed -i 's/^GRUB_CMDLINE_LINUX_DEFAULT=.*/GRUB_CMDLINE_LINUX_DEFAULT="quiet splash loglevel=3 vt.global_cursor_default=0"/' /etc/default/grub
        update-grub || true
    fi

    systemctl daemon-reload
    systemctl restart nvr-kiosk.service 2>/dev/null || true
fi

log "vainfo (диагностика QSV/VAAPI):"
vainfo 2>&1 | sed -n '1,20p' || true

log "Готово. Проверьте статус: systemctl status nvr-prototype"
