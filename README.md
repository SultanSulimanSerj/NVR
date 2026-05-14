# NVR Prototype (C++/FFmpeg/OpenCV/pybind11) — Trassir-OS-подобная платформа

Прототип высокопроизводительного NVR-движка на C++ под Ubuntu Server + Intel i7,
с веб-интерфейсом, HLS-live, ONVIF, многоканальными уведомлениями и kiosk-режимом
на HDMI.

## Стек

| Слой           | Технология |
|----------------|-----------|
| Ядро           | C++20, `std::thread`/jthread-style stop signals, `FFmpeg` (QSV/VAAPI), `OpenCV` |
| Хранилище конфигурации | SQLite (через `SQLiteCpp`), шифрование секретов `libsodium` |
| HTTP/WS API    | `Crow`, JWT (`jwt-cpp`), TLS reverse-proxy; `http_worker_threads` in `nvr.yaml` maps to Crow worker count (≥2) |
| Видеотрансляция | HLS / LL-HLS fmp4 (sub-stream encoder) |
| ONVIF          | WS-Discovery (UDP 3702 multicast) + SOAP клиент (`libcurl`) |
| Уведомления    | Email (`libcurl` SMTP), Telegram, Webhook; MQTT — см. [docs/MQTT_STATUS.md](docs/MQTT_STATUS.md) |
| Аналитика      | OpenCV MOG2 + ROI-маски + Pre/Post-event буфер |
| Скрипты        | Python через `pybind11::embed` (numpy кадр + метаданные) |
| Метрики        | Prometheus (`/metrics`): глобальные счётчики, per-camera, **`nvr_analytics_*`** (очередь фоновой аналитики) + `/api/v1/health` |
| Web UI         | React + Vite + TS + Tailwind + TanStack Query + hls.js (ru/en) |
| Kiosk          | `cage` на **tty1**, **`nvr-shell`** (WebKitGTK) или Chromium kiosk; URL **https://nvr.local** (TLS на nginx, порт 443); пользователь `operator` (DRM `video`/`render`); резервная консоль на **tty2** (**Ctrl+Alt+F2**), возврат на киоск **Ctrl+Alt+F1** |
| CLI            | `nvr-cli` (HTTP-клиент) |
| Безопасность   | systemd hardening, `ufw`, `unattended-upgrades`, бэкап/восстановление |

## Возможности

- Многопоточный конвейер — **один поток на камеру** (`std::thread`, кооперативная остановка).
- Захват RTSP через **FFmpeg** (`avformat`/`avcodec`), TCP-транспорт, авто-переподключение с экспоненциальным backoff.
- **Аппаратный декод**: `h264_qsv`/`hevc_qsv` (Intel Quick Sync) с фолбэком на `h264_vaapi`/`hevc_vaapi` и далее на программный декодер. Управляется ключом `preferred_hw: qsv|vaapi|none|auto` в конфиге.
- **Кольцевая запись архива** сегментами (по умолчанию 60 минут, MP4 remux без перекодирования) и автоудаление старейших сегментов при достижении 80% занятости диска (см. `ArchiveManager`).
- **Детектор движения** (OpenCV `BackgroundSubtractorMOG2`) работает на даунскейле (640×360, 5–10 FPS), выдаёт события с `area_ratio` и сохраняет JPEG-снимок.
- **Python-хуки** через `pybind11::embed`: один интерпретатор, отдельный worker-поток, очередь событий с capacity-лимитом — camera-потоки не блокируются GIL. В хук передаётся метаданные + путь к снимку; при `include_frame: true` — ещё и `numpy.ndarray` (BGR).
- **systemd-юнит** с ужесточёнными правами (`ProtectSystem=full`, `NoNewPrivileges`, фильтры) и доступом только к `/dev/dri`.
- Подготовленный `sysctl`-набор и опциональный **kiosk-режим** ОС: полноэкранный клиент на **tty1**, маскирование `getty@tty1` и **tty3–tty6** (на **tty2** остаётся getty для аварийного входа), запрет Magic SysRq, тихий GRUB; администрирование по **SSH**, **serial** или **Ctrl+Alt+F2**.

## Архитектура

```
+--------------------- main process ---------------------+
| ArchiveManager (retention thread, statvfs, eviction)   |
| PythonHookManager  (1 worker, 1 interpreter, GIL safe) |
|                                                        |
|   ThreadSafeQueue<MotionEvent>  <----+                 |
|                                       \                |
|   per camera:                          \               |
|   +------------------------+            \              |
|   | CameraPipeline thread  | -- enqueue motion event   |
|   |  FFmpegDecoder (HW)    |                           |
|   |  SegmentWriter (mux)   |                           |
|   |  MotionDetector (CV)   |                           |
|   +------------------------+                           |
+--------------------------------------------------------+
```

## Структура проекта

- `src/main.cpp` — точка входа, инициализация, SIGINT/SIGTERM.
- `src/core/Config.cpp`, `include/nvr/Config.hpp` — YAML-конфиг.
- `src/core/Logger.cpp`, `include/nvr/Logger.hpp` — простой потокобезопасный лог.
- `include/nvr/ThreadSafeQueue.hpp` — очередь с drop-on-overflow и закрытием.
- `src/archive/ArchiveManager.cpp`, `include/nvr/ArchiveManager.hpp` — сегменты + retention.
- `src/camera/FFmpegDecoder.cpp` — RTSP demux + декод (QSV/VAAPI/SW).
- `src/camera/FFmpegEncoder.cpp` — `SegmentWriter` (MP4 remux, ротация).
- `src/camera/CameraPipeline.cpp` — конвейер на камеру.
- `src/analytics/MotionDetector.cpp` — даунскейл + MOG2 + JPEG snapshot.
- `src/python/PythonHookManager.cpp` — pybind11 embed + worker + numpy.
- `config/nvr.yaml` — пример конфигурации.
- `scripts/setup_ubuntu_nvr.sh` — установка зависимостей, сборка, systemd, sysctl, kiosk.
- `scripts/nvr_kiosk_launcher.sh`, `scripts/nvr_kiosk_bootstrap.sh` — запуск UI в `cage` (Chromium или `nvr-shell`).
- `scripts/build_nvr_shell.sh` — сборка нативного клиента киоска (Rust + WebKitGTK).
- `nvr-shell/` — исходники `nvr-shell` (опция CMake `-DNVR_BUILD_KIOSK_SHELL=ON`).
- `scripts/script.py` — пример хука.
- `deploy/nvr-prototype.service`, `deploy/nvr-kiosk.service`, `deploy/nvr-prototype.sysctl.conf` — артефакты деплоя.

### Архив и nginx (X-Accel)

`deploy/nginx/nvr.conf` подключает сниппет `/etc/nvr-prototype/nginx-archive-accel.conf`. После смены `archive.root_path` в `nvr.yaml` выполните `sudo nvr_sync_nginx_archive_accel.sh`, затем `sudo nginx -t && sudo systemctl reload nginx` (пакет вызывает скрипт из `postinst`).

### CI: нагрузка и e2e (опционально)

- **Операции на узле:** [docs/RUNBOOK.md](docs/RUNBOOK.md) (установка, обновление, откат, nginx, бэкап) и [docs/LOADTEST_SLO.md](docs/LOADTEST_SLO.md) (k6, матрица SLO «каналы × разрешение»).
- **k6:** Actions → *Load test (k6 smoke)*; в настройках репозитория задайте переменную `NVR_LOADTEST_BASE_URL` (origin, без пути). Используется `scripts/loadtest/k6-health.js`; сценарий с логином и HLS — `scripts/loadtest/k6-smoke.js` (`NVR_USER`, `NVR_PASS`).
- **Playwright:** в `.github/workflows/ci.yml` job `e2e` при непустом `NVR_E2E_BASE_URL` и секрете `NVR_E2E_ADMIN_PASSWORD`.
- **Перед установкой на регистратор:** чеклист и матрица ручного регресса — [docs/PRE_FIELD_AUDIT_2026-05-13.md](docs/PRE_FIELD_AUDIT_2026-05-13.md).
- **Аудит разрывов vs Trassir OS и долгая дорожная карта:** [docs/TRASSIR_PARITY_ROADMAP.md](docs/TRASSIR_PARITY_ROADMAP.md).

## Сборка вручную

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
    libopencv-dev python3-dev python3-numpy libyaml-cpp-dev \
    intel-media-va-driver-non-free vainfo

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Для сборки нативного клиента киоска `nvr-shell`: установите `cargo`, `libwebkit2gtk-4.1-dev`, `libgtk-3-dev`, затем повторите `cmake` с `-DNVR_BUILD_KIOSK_SHELL=ON` (см. также `scripts/build_nvr_shell.sh`).

Бинарь: `./build/nvr_prototype config/nvr.yaml`.

## Установка на сервер через GitHub

Репозиторий: [https://github.com/SultanSulimanSerj/NVR](https://github.com/SultanSulimanSerj/NVR).

1. На сервере один раз поставьте **nginx** (скрипт setup только копирует конфиги): `sudo apt-get install -y nginx`.
2. Синхронизация кода: `chmod +x scripts/server_sync_from_github.sh && ./scripts/server_sync_from_github.sh`  
   (или `NVR_GIT_URL=… NVR_CLONE_DIR=…` при необходимости).
3. В **втором** SSH-сеансе запустите сторож, чтобы видеть «живую» сборку и сеть/git:  
   `chmod +x scripts/build_hang_watch.sh && ./scripts/build_hang_watch.sh`  
   (интервал в секундах: `./scripts/build_hang_watch.sh 30`).
4. В **первом** сеансе: `cd ~/nvr-prototype && sudo ./scripts/setup_ubuntu_nvr.sh` (или с `--kiosk`).  
   Долгое ожидание при **git-remote-http** и **cc=0** — часто клонирование зависимостей CMake с GitHub, а не зависание.

## Полная установка одной командой

```bash
sudo ./scripts/setup_ubuntu_nvr.sh           # сервер
sudo ./scripts/setup_ubuntu_nvr.sh --kiosk   # сервер + kiosk-режим ОС
```

Проверка QSV/VAAPI: `vainfo`. Лог сервиса: `journalctl -u nvr-prototype -f`.

### Kiosk (как у VMS-клиента)

После `sudo ./scripts/setup_ubuntu_nvr.sh --kiosk` на HDMI сразу открывается полноэкранный интерфейс на **https://nvr.local** (тот же Web UI, без отдельной «консоли журнала» на tty1). Имя `nvr.local` резолвится в `127.0.0.1`, сертификат TLS содержит SAN `DNS:nvr.local`, корневой сертификат добавляется в системное хранилище (`update-ca-certificates`).

- **Автозапуск при загрузке:** юнит `nvr-kiosk` включён в `multi-user.target` (и в `graphical.target`), чтобы киоск поднимался на типичном Ubuntu Server без смены default target. На **регистраторе** при наличии файла-маркера **`/etc/nvr-prototype/appliance`** `postinst` пакета дополнительно включает `nvr-kiosk` и `seatd` и делает `try-restart` (киоск стартует только при наличии узла DRM: **`/dev/dri/card0`**, **`card1`** или **`renderD128`** — см. `ExecCondition` в юните). Без маркера на обычной установке киоск не включается из пакета автоматически; используйте `--kiosk` или `systemctl enable --now nvr-kiosk seatd`. Пакет помечает **`cage`**, **`seatd`** и **Chromium** как *Recommends* — при `apt install` их стоит не отключать на узле с HDMI-киоском.
- **Резервная консоль:** **Ctrl+Alt+F2** — **tty2** с обычным приглашением `getty`; **Ctrl+Alt+F1** — обратно на киоск (**tty1**).
- **Локальный дашборд в браузере:** **`/local-dashboard`** без **`kiosk_token`** и без сохранённой сессии перенаправляет на **`/login`** (нет «пустого» грида и лишних 401).
- Скрипты киоска: `/usr/share/nvr-prototype/scripts/nvr_kiosk_launcher.sh` (предпочитает **`/usr/bin/nvr-shell`**, если установлен из `.deb`/сборки с `-DNVR_BUILD_KIOSK_SHELL=ON`, иначе Chromium с флагами kiosk).
- Токен киоска: `/etc/nvr-prototype/kiosk.token`; bootstrap: `nvr_kiosk_bootstrap.sh` (опционально `KIOSK_PROVISION_PASSWORD`).
- Переменные окружения юнита: `NVR_KIOSK_BASE_URL`, `NVR_KIOSK_API_BASE` (по умолчанию `https://nvr.local`).
- Сборка `nvr-shell` вручную: `sudo apt install cargo libwebkit2gtk-4.1-dev libgtk-3-dev` и `./scripts/build_nvr_shell.sh`, затем `sudo install -m0755 nvr-shell/target/release/nvr-shell /usr/bin/`.

## Конфигурация (фрагмент)

```yaml
archive:
  root_path: /var/lib/nvr-prototype/archive
  segment_minutes: 60
  target_usage_ratio: 0.80
  release_to_ratio: 0.78
python:
  enabled: true
  script_path: /usr/share/nvr-prototype/scripts/script.py
  include_frame: false       # true => в хук пойдёт numpy.ndarray (BGR), дороже по GIL
cameras:
  - id: cam01
    rtsp_url: "rtsp://user:pass@192.168.1.10:554/Streaming/Channels/101"
    preferred_hw: qsv
```

## Ограничения прототипа

- `SegmentWriter` работает в режиме **remux** (без перекодирования) — оптимально по CPU, но требует, чтобы кодек камеры совпадал с допустимым в MP4 (H.264/H.265). Для смены кодека потребуется добавить hw-энкод (`h264_qsv`/`h264_vaapi`).
- HW-декод требует доступа к `/dev/dri/renderD128` (группы `video`/`render`). В Docker — пробрасывать устройства.
- Лимит диска вычисляется по mount point архива через `statvfs`. Удаляются только файлы с собственным префиксом/расширением.
- Один Python-интерпретатор на процесс; долгие хуки могут переполнить очередь — отслеживайте `dropped_count` в логе.
