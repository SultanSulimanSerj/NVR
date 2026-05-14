# Runbook: NVR prototype — установка, эксплуатация, откат

Единый операторский документ для узла **nvr_prototype** + **nginx** (TLS, reverse-proxy). Детальный чеклист перед выездом на регистратор — [PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md). Нагрузка и матрица SLO — [LOADTEST_SLO.md](LOADTEST_SLO.md). Внешняя аутентификация (план) — [SECURITY_EXTERNAL_AUTH.md](SECURITY_EXTERNAL_AUTH.md). Логи и logrotate — [LOGROTATE_HOOKS.md](LOGROTATE_HOOKS.md). Crow / потоки HTTP — [HTTP_CONCURRENCY.md](HTTP_CONCURRENCY.md).

## Содержание

1. [Установка и first-run](#1-установка-и-first-run)
2. [Обновление и откат](#2-обновление-и-откат)
3. [Бэкап и восстановление БД](#3-бэкап-и-восстановление-бд)
4. [nginx, TLS, архив X-Accel](#4-nginx-tls-архив-x-accel)
5. [Лицензия](#5-лицензия)
6. [Миграции схемы БД](#6-миграции-схемы-бд)
7. [Смоук k6 (опционально)](#7-смоук-k6-опционально)
8. [Типовые сбои](#8-типовые-сбои)

---

## 1. Установка и first-run

**Вариант A — скрипт с исходников (разработка / lab):**

```bash
sudo ./scripts/setup_ubuntu_nvr.sh           # сервер
sudo ./scripts/setup_ubuntu_nvr.sh --kiosk   # + киоск на tty1
```

Скрипт ставит зависимости, создаёт пользователя `nvr`, каталоги, systemd-юнит, при необходимости **`/etc/nvr-prototype/setup.token`** (`0640`, группа `nvr`) для мастера настройки.

**Вариант B — пакет `.deb`:** установка через `apt` / `dpkg`; постинст настраивает права на секреты и при необходимости запускает `--migrate-only` (см. `deploy/debian/postinst`). Если на узле есть маркер **`/etc/nvr-prototype/appliance`**, постинст также включает **`seatd`** и **`nvr-kiosk`** (подробности в разделе **1.2** ниже).

**First-run:**

1. Откройте веб-интерфейс по **https://nvr.local** (или IP), пройдите мастер с заголовком **`X-Setup-Token`** = содержимое `setup.token`.
2. Задайте пароль admin, при необходимости hostname, TZ, корень архива.
3. После finalize токен удаляется; вход по обычному логину.

Подробнее про киоск: [README.md](../README.md) раздел Kiosk.

### 1.1 Справочник совместимости камер (опционально)

- Пример JSON в репозитории: [deploy/camera_catalog.sample.json](../deploy/camera_catalog.sample.json) (в пакете: `/usr/share/nvr-prototype/camera_catalog.sample.json`).
- Рабочий файл на узле: **`/etc/nvr-prototype/camera_catalog.json`** или путь из переменной окружения **`NVR_CAMERA_CATALOG_PATH`** для службы `nvr-prototype`.
- API: **`GET /api/v1/system/camera-catalog`** (роль viewer+); таблица на странице **Камеры** в веб-интерфейсе.

### 1.2 Киоск: маркер `appliance`, автозапуск, резервная консоль

- **Маркер регистратора:** если на узле есть файл **`/etc/nvr-prototype/appliance`** (достаточно пустого файла; обычно создаётся образом autoinstall или один раз вручную до/после первой установки пакета), скрипт **`deploy/debian/postinst`** при установке/обновлении пакета выполняет `systemctl enable` для **`seatd`** и **`nvr-kiosk`**, затем **`try-restart`** обоих. Юнит **`nvr-kiosk`** не стартует без доступного DRM-узла под **`/dev/dri/`** (см. `ExecCondition` в unit-файле: `card0`, `card1` или `renderD128`).
- **Без маркера** киоск из `postinst` не включается: для lab используйте **`sudo ./scripts/setup_ubuntu_nvr.sh --kiosk`** или вручную **`systemctl enable --now seatd nvr-kiosk`** (и зависимости cage/DRM по политике узла).
- **TTY и клавиши:** киоск на **tty1**; **Ctrl+Alt+F2** — **tty2** с **`getty`** (логин по политике заказчика); **Ctrl+Alt+F1** — возврат на киоск. Скрипт `setup_ubuntu_nvr.sh --kiosk` маскирует лишние консоли **tty3–tty6** и **tty1** (под киоск), **не** трогая **tty2**.

---

## 2. Обновление и откат

Скрипт **`scripts/nvr_update.sh`** (в пакете обычно в `/usr/share/nvr-prototype/scripts/`):

- `--check` — проверить наличие обновления.
- `--apply` — скачать и установить новый пакет; **перед установкой** сохраняется снимок **текущего** установленного `.deb` для отката (не вновь скачанный файл).
- `--rollback` — установить сохранённый предыдущий пакет из кеша.

Перед обновлением на площадке рекомендуется **снять копию БД** (см. раздел 3). После обновления: `systemctl status nvr-prototype`, `journalctl -u nvr-prototype -n 100 --no-pager`.

---

## 3. Бэкап и восстановление БД

- Скрипты **`/usr/bin/nvr_backup.sh`** / **`/usr/bin/nvr_restore.sh`** (или **`nvr-cli backup`** / **`nvr-cli restore <path>`** — вызывают те же скрипты без shell-склейки).
- Убедитесь, что путь бэкапа на отдельном носителе или сетевом хранилище согласован с политикой заказчика.

---

## 4. nginx, TLS, архив X-Accel

Конфиг: **`deploy/nginx/nvr.conf`** (на узле — подключение через snippets TLS). Демон слушает **127.0.0.1:8080**; снаружи только **443** на nginx.

**Смена `archive.root_path`:** выполните **`sudo nvr_sync_nginx_archive_accel.sh`**, затем **`sudo nginx -t && sudo systemctl reload nginx`**. Сниппет **`/etc/nvr-prototype/nginx-archive-accel.conf`** должен соответствовать основному корню архива; **X-Accel-Redirect** только для файлов под этим корнем (дополнительные корни — см. [TRASSIR_PARITY_ROADMAP.md](TRASSIR_PARITY_ROADMAP.md) §2.1).

---

## 5. Лицензия

Файлы и биты модулей — см. [LICENSE_TICKET.md](LICENSE_TICKET.md). В UI: раздел **Система → Лицензия**, API `GET /api/v1/license`.

---

## 6. Миграции схемы БД

Бинарь применяет миграции при старте. Ручной прогон:

```bash
/usr/bin/nvr_prototype --migrate-only /etc/nvr-prototype/nvr.yaml
```

При ненулевом коде выхода демон не запускайте до исправления БД/диска.

---

## 7. Смоук k6 (опционально)

Быстрая проверка доступности API за nginx:

```bash
export NVR_BASE_URL=https://nvr.local
k6 run scripts/loadtest/k6-health.js
```

Локально к демону (минуя nginx):

```bash
k6 run scripts/k6-nvr-smoke.js
# или: BASE_URL=http://127.0.0.1:8080 k6 run scripts/k6-nvr-smoke.js
```

Полный смоук с логином и HLS — `scripts/loadtest/k6-smoke.js` (`NVR_USER`, `NVR_PASS`). Подробности и матрица нагрузки — [LOADTEST_SLO.md](LOADTEST_SLO.md).

---

## 8. Типовые сбои

### Сервис не поднимается после обновления пакета

1. Миграции вручную: см. [раздел 6](#6-миграции-схемы-бд).
2. Лог: `journalctl -u nvr-prototype -n 200 --no-pager`.
3. Каталоги из `nvr.yaml`: `archive.root_path`, путь к БД, `jwt_secret_file` — существуют и доступны пользователю **`nvr`**.

### Диск архива заполнен / сегменты на дополнительном корне

1. В API/UI задайте **`extra_archive_roots`** и основной **`root_path`**; новые сегменты выбирают корень с большим свободным местом.
2. Сегменты на **дополнительном** диске отдаются по HTTP из процесса; **X-Accel-Redirect** только для основного **`root_path`**.
3. Квота и eviction учитывают **все** корни.

### Экспорт диапазона без водяного знака / с артефактами

1. **`export_watermark_text`** в конфиге архива включает второй проход `ffmpeg` с `drawtext`. В **`POST /api/v1/archive/export`** можно передать **`watermark_text`** в JSON — переопределение на один запрос; пустая строка отключает оверлей для этого экспорта. При сбое drawtext клиенту отдаётся результат concat **без** оверлея.
2. На хосте должен быть **`ffmpeg`** в `$PATH`.

---

## Перед выездом на регистратор

Выполните чеклист **[PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md)** (P0 обязательно; P1/P2 по возможности). Укажите дату прогона в том файле или в приложенном отчёте.
