# Аудит разрывов и дорожная карта: паритет с Trassir OS

**Дата:** 2026-05-13 (актуализация содержания §2.1–2.6 — 2026-05-13)  
**Контекст:** текущий репозиторий — **прототип ядра NVR** (C++/FFmpeg/OpenCV, веб-UI, один узел). Цель заявлена как **полноценный аналог Trassir OS** по функционалу. Ниже — честная оценка разрыва и **крупный план доработок**, разбитый по осям продукта и фазам.

Связанные документы: [AUDIT_2026-05.md](AUDIT_2026-05.md), [PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md), [PLAN_RECORDER_ANALYTICS_LICENSE.md](PLAN_RECORDER_ANALYTICS_LICENSE.md), [LICENSE_TICKET.md](LICENSE_TICKET.md), [RUNBOOK.md](RUNBOOK.md), [LOADTEST_SLO.md](LOADTEST_SLO.md), [TRASSIR_POLISH_PHASES.md](TRASSIR_POLISH_PHASES.md), [TRASSIR_HORIZON1_BACKLOG.md](TRASSIR_HORIZON1_BACKLOG.md), [TRASSIR_ROI_RECORDING_SCOPE.md](TRASSIR_ROI_RECORDING_SCOPE.md), [TRASSIR_HORIZON2_ANALYTICS.md](TRASSIR_HORIZON2_ANALYTICS.md), [TRASSIR_HORIZON3_CORP_UI.md](TRASSIR_HORIZON3_CORP_UI.md), [ARCHITECTURE_TRASSIR_PLATFORM.md](ARCHITECTURE_TRASSIR_PLATFORM.md).

---

## 1. Краткий вывод аудита

| Измерение | Сейчас (репозиторий) | Trassir OS (типичные ожидания рынка) | Разрыв |
|-----------|----------------------|----------------------------------------|--------|
| **Ядро NVR** | Запись сегментами, HLS live, архив, motion+ROI, уведомления, RBAC, лицензия каналов (скелет) | То же + зрелые политики записи, таймлайн, экспорт, тонкая матрица прав | Средний: много «классики» есть, не хватает полировки UX и сценариев |
| **Аналитика** | MOG2, задел AI/лиц в API/БД, Python-хуки | Линии/зоны, LPR, трекинг, стабильные модули под железо, сертификация моделей | **Большой** |
| **Платформа** | Один процесс, SQLite, Crow | Кластер, репликация, облако, централизованное управление | **Очень большой** |
| **Экосистема** | Web UI, kiosk, CLI | Мобильные клиенты, облачный доступ, SDK для интеграторов, каталог камер | **Очень большой** |
| **Коммерция** | Офлайн-лицензия (Ed255), trial | Каталог SKU, активация, учёт, телеметрия поля | Средний (база заложена) |
| **Эксплуатация** | systemd, nginx, AppArmor, скрипты | Runbook, мониторинг fleet, SLA, удалённая диагностика | Средний (процессы и доки) |

**Итог:** как **локальный регистратор на одном сервере** проект может приблизиться к ощущению «Trassir-подобного» продукта за **1–2 года** целенаправленной разработки и полевых итераций. Как **полный аналог экосистемы Trassir OS** (облако + мобилки + кластер + маркетплейс аналитики) — это **несколько лет** и **отдельная команда/продуктовая линия**, не один репозиторий без расширения scope.

Ниже план структурирован так: сначала **максимальный паритет по «коробке регистратора»**, затем **аналитика и коммерция**, затем **платформа и экосистема**.

---

## 2. Детальный аудит по областям

### 2.1 Запись, архив, live

**Есть:** поток на камеру, remux в MP4, retention, HLS, Range/X-Accel (nginx), pre/post event, субпоток; **PreEventBuffer** подключён в пайплайне при `pre_event_seconds > 0` ([CameraPipeline.cpp](../src/camera/CameraPipeline.cpp)); режим **`hybrid`** (непрерывный mux + маркировка движением при `enable_motion`); расписание по неделям в UI/API.

**Закрыто в «фазе 1 polish» (см. [TRASSIR_POLISH_PHASES.md](TRASSIR_POLISH_PHASES.md)):**

- **Гибрид записи** в API/UI с проверкой `enable_motion`.
- **Таймлайн:** `GET /api/v1/archive/timeline` — `event_count` по бакетам; в UI — две полосы (движение / события), клик по бакету сужает интервал «С/По»; события с клипом — кнопка «±45 с» к экспорту.
- **Мультидиск:** `GET /api/v1/archive/roots-health` (API) и карточка на странице «Хранилище» (Web UI).
- **Экспорт:** диапазон, аудит, RBAC, водяной знак (`export_watermark_text` / `watermark_text` в `POST /api/v1/archive/export`).

**По-прежнему не хватает для паритета «как у зрелого VMS»** (бэклог — [TRASSIR_HORIZON1_BACKLOG.md](TRASSIR_HORIZON1_BACKLOG.md)):

- **Политики по ROI/зонам** — отдельно от глобального режима камеры; продуктовый scope: [TRASSIR_ROI_RECORDING_SCOPE.md](TRASSIR_ROI_RECORDING_SCOPE.md).
- **Таймлайн:** цвета по типам событий, прямой переход «точка события → готовый MP4 клипа» без ручного экспорта диапазона.
- **Мультидиск:** миграция сегментов при смене корня, оповещения при `enospc`/I/O, формализованный SLA в [SLO.md](SLO.md).

**Привязка к коду:** [src/archive/ArchiveManager.cpp](src/archive/ArchiveManager.cpp), [src/camera/CameraPipeline.cpp](src/camera/CameraPipeline.cpp), [webui/src/pages/Archive.tsx](webui/src/pages/Archive.tsx), [src/api/RouteArchiveEvents.cpp](src/api/RouteArchiveEvents.cpp).

---

### 2.2 Камеры, ONVIF, PTZ

**Есть:** RTSP, ONVIF discovery, PTZ endpoint, камеры в БД/YAML; **POST `/api/v1/onvif/device-time-sync`** (UTC с хоста); **POST `/api/v1/cameras/{id}/onvif-events/pull-once`** (CreatePullPoint + PullMessages, с **повторными попытками** SOAP в [EventsPull.cpp](../src/onvif/EventsPull.cpp)); ingest и листинг onvif-событий; **GET `/api/v1/system/camera-catalog`** — чтение JSON-массива с пути `NVR_CAMERA_CATALOG_PATH` или `/etc/nvr-prototype/camera_catalog.json` (MVP справочника; пример — [deploy/camera_catalog.sample.json](../deploy/camera_catalog.sample.json)).

**Не хватает для «как у Trassir»:**

- Полноценный **provisioning** (синхронизация профилей Media/Imaging, массовое применение пресетов).
- **Каталог** как продукт: редакция в UI, версии, привязка к прошивкам (сейчас — статический JSON на диске).
- **Матрица вендоров** PullPoint/digest/namespace (см. [ONVIF_PULLPOINT_CAMERA_MATRIX.md](ONVIF_PULLPOINT_CAMERA_MATRIX.md) и горизонт 1 в [TRASSIR_HORIZON1_BACKLOG.md](TRASSIR_HORIZON1_BACKLOG.md)).

**Обновление в репозитории:** `GET /api/v1/cameras/{id}/stream-stats`, `GET /api/v1/cameras/{id}/onvif-events`, ingest, UI метрик на [Cameras.tsx](webui/src/pages/Cameras.tsx).

**Код:** [src/onvif/](src/onvif/), [webui/src/pages/Cameras.tsx](webui/src/pages/Cameras.tsx), [src/api/RouteCamerasLicense.cpp](src/api/RouteCamerasLicense.cpp).

---

### 2.3 Пользователи, безопасность, соответствие

**Есть:** JWT, refresh rotation, TOTP, роли `admin` / `operator` / `viewer`, аудит, hardening; **пер-камерная ACL** и проверки в маршрутах API (см. матрицу в [PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md)); задел **SNMP/syslog** и подписанных webhooks в уведомлениях (см. код `notify` / конфиг каналов).

**Не хватает / частично:**

- **LDAP/AD и OIDC:** в API пока **заглушка статуса** (`/api/v1/auth/external`); детальный дизайн — [SECURITY_EXTERNAL_AUTH.md](SECURITY_EXTERNAL_AUTH.md). Реализация bind/code-flow — в горизонте 3 / корпоративный контур.
- **Тонкая матрица по зонам/аналитике** (не только по камерам).
- Юридически оформленные режимы для **LPR/биометрии** (152-ФЗ и т.д.) — [TRASSIR_HORIZON2_ANALYTICS.md](TRASSIR_HORIZON2_ANALYTICS.md).

**Код:** [src/api/Auth.cpp](src/api/Auth.cpp), [docs/SECURITY.md](docs/SECURITY.md).

---

### 2.4 Видеоаналитика

**Есть:** Motion MOG2 + ROI, таблицы/эндпоинты под AI, Python-хуки, **`AnalyticsWorker`** (очередь, метрики `nvr_analytics_*`), поиск детекций в API, биты лицензии на AI-операциях (см. [LICENSE_TICKET.md](LICENSE_TICKET.md)).

**Не хватает (ядро Trassir-подобной ценности)** — см. дорожку и критерии в [TRASSIR_HORIZON2_ANALYTICS.md](TRASSIR_HORIZON2_ANALYTICS.md):

- Стабильные **SKU-модули** (линия/зона, объекты, LPR, лица) end-to-end с полевыми метриками FP/FN.
- **ONNX/OpenVINO** (или аналог) с лимитами FPS и жёсткой привязкой к `modules_bitmask`.

---

### 2.5 Уведомления и интеграции

**Есть:** Email, Telegram, Webhook (в т.ч. подпись где реализовано в канале), DLQ в UI, retry; **MQTT**: реализация TCP publish QoS0 ([MqttTcp311.cpp](../src/notify/MqttTcp311.cpp)); включение доставки через настройку **`features.mqtt_delivery=live`** в БД ([RouteConfig.cpp](src/api/RouteConfig.cpp)); тип канала `mqtt` в [NotificationManager](src/notify/NotificationManager.cpp).

**Не хватает для паритета с крупными VMS:** сертифицированные сценарии на брокерах, observability очередей MQTT, готовые коннекторы PSIM/ticketing «из коробки».

---

### 2.6 UI / UX / киоск

**Есть:** React, i18n, RBAC, лицензия в System/Dashboard, offline banner, lazy routes; **карта** `/map` с `plan_x`/`plan_y` и **фоном по URL** (`GET/PUT /api/v1/config/map`, `plan_image_url`); **киоск** `LocalDashboard` — query `?grid` / `?cols`, локальные пресеты и **синхронизация с `GET/POST/DELETE /api/v1/live-layouts`** ([LocalDashboard.tsx](webui/src/pages/LocalDashboard.tsx)); **field bundle** для поддержки (`GET /api/v1/system/field-bundle`).

**Не хватает (фаза 3 polish — см. [TRASSIR_HORIZON3_CORP_UI.md](TRASSIR_HORIZON3_CORP_UI.md)):**

- Многоэтажная карта, drag камер на плане, **a11y** и крупные touch-таргеты, отдельный **режим оператора** на HDMI, версии макетов live.

---

### 2.7 Платформа и масштаб (отличие от «один рег»)

**Отсутствует в архитектуре:** HA-кластер, репликация записи, **Trassir Cloud**-подобный доступ, **единый сервер лицензий/активации** для парка устройств, **federation** нескольких NVR.

Это **отдельные продуктовые решения** (микросервисы, очереди, object storage, edge sync).

---

### 2.8 Экосистема (мобилки, SDK, каталог)

**Отсутствует:** нативные **iOS/Android**, **публичный SDK** (C#/JS) для интеграторов, **маркетплейс** модулей.

---

### 2.9 Качество, тесты, поле

**Есть:** юнит-тесты, PRE_FIELD чеклист, опциональные k6/e2e.

**Артефакты:**

- [RUNBOOK.md](RUNBOOK.md) — установка, обновление, откат, nginx, бэкап, типовые сбои + ссылка на PRE_FIELD.
- [LOADTEST_SLO.md](LOADTEST_SLO.md) — сценарии k6, переменные окружения, **матрица каналов × разрешение** и ориентиры SLO для полевого прогона.

**Не хватает:** интеграционные тесты HTTP, регресс **по матрице камер × кодеки**, закреплённый стенд в CI с полным k6-профилем (сейчас — `workflow_dispatch` + health-only по умолчанию).

---

## 3. Большой план доработки (фазы)

Оценки — **порядок величины** (команда уровня 4–8 инженеров); реальные сроки зависят от headcount и железа.

### Фаза A — «Коробка регистратора v1» (6–12 мес.)

Цель: **не уступать** по базовым сценариям сильным одноузловым NVR на поле.

| # | Направление | Результат |
|---|-------------|-----------|
| A1 | Политики записи + UI | Расписание, тип записи, отображение в статусе камеры |
| A2 | Архив: таймлайн + поиск | Фильтры по типу события, переход к клипу |
| A3 | Экспорт + аудит | Ограничения по роли, лог выгрузок |
| A4 | ONVIF углубление | Events с камеры, профили, time sync |
| A5 | Нагрузка и SLO | Матрица «каналы × разрешение», k6 в регламенте — [LOADTEST_SLO.md](LOADTEST_SLO.md) |
| A6 | Runbook + PRE_FIELD | [RUNBOOK.md](RUNBOOK.md) + [PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md) |

### Фаза B — «Аналитика как продукт» (12–24 мес.)

| # | Направление | Результат |
|---|-------------|-----------|
| B1 | Конвейер аналитики | Очередь кадров, ONNX/OpenVINO, лимиты CPU/GPU |
| B2 | Модули SKU | Линия/зона, объекты, LPR, лица — связка с [LicenseGate](src/core/LicenseGate.cpp) |
| B3 | Качество на поле | Датасеты, метрики false positive, калибровка по заказчику |
| B4 | Юридика | Режимы хранения биометрии, согласия, экспорт по запросу субъекта |

### Фаза C — «Корпоративный VMS» (12–18 мес. параллельно с B частично)

| # | Направление | Результат |
|---|-------------|-----------|
| C1 | LDAP/OIDC | Вход и группы → роли |
| C2 | Матрица прав | По камерам и операциям |
| C3 | SNMP/syslog | Интеграция с NOC/SIEM |
| C4 | Карта и макеты | План объекта, сохранённые сетки live — **частично закрыто** фазой 1 polish ([TRASSIR_POLISH_PHASES.md](TRASSIR_POLISH_PHASES.md)); многоэтажность — горизонт 3 |

### Фаза D — «Платформа Trassir-класса» (24+ мес., отдельная программа)

| # | Направление | Результат |
|---|-------------|-----------|
| D1 | Центральный сервер | Учёт лицензий, конфигов, обновлений парка |
| D2 | Облако / удалённый доступ | Безопасный туннель, роли, биллинг |
| D3 | Кластер / HA | Репликация, split-brain политики |
| D4 | Мобильные клиенты | iOS/Android, push |
| D5 | SDK и партнёры | Стабильный API, примеры, сертификация интеграций |

---

## 4. Приоритизация внутри репозитория (технический backlog)

Ниже — **логический порядок** работ в текущем монорепо без обязательного разделения на микросервисы.

1. **Рефакторинг HTTP-слоя** — декомпозиция [HttpServer.cpp](src/api/HttpServer.cpp) по доменам (auth, cameras, archive, system) для снижения стоимости регрессий.
2. **Политики записи** — модель в БД + API + UI; связь с [CameraPipeline](src/camera/CameraPipeline.cpp).
3. **Таймлайн архива** — агрегация событий + UI ([Archive.tsx](webui/src/pages/Archive.tsx)).
4. **Конвейер аналитики** — отдельный worker-процесс или пул потоков + контракт событий (расширение [Database](src/store/Database.cpp)).
5. **Первый NN-модуль** — один тип (например пересечение линии на геометрии или простой детектор ONNX).
6. **L4 лицензий** — привязка битов `mod` к AI-эндпоинтам ([RouteApplication.cpp](../src/api/RouteApplication.cpp), [LICENSE_TICKET.md](LICENSE_TICKET.md)).
7. **Карта и макеты:** фон плана по URL, координаты камер, серверные макеты live — см. [TRASSIR_POLISH_PHASES.md](TRASSIR_POLISH_PHASES.md) фаза 1; многоэтажность и drag — фаза 3 ([TRASSIR_HORIZON3_CORP_UI.md](TRASSIR_HORIZON3_CORP_UI.md)).
8. **Интеграции** — LDAP/OIDC (см. [SECURITY_EXTERNAL_AUTH.md](SECURITY_EXTERNAL_AUTH.md)), расширение SNMP после стабилизации ядра.

---

## 5. Риски

- **Ожидание «всё как Trassir» за один релиз** — размывает качество; лучше объявлять **версии паритета** (например «Parity-Recorder-1.0»).
- **Аналитика без полевых данных** — много ложных срабатываний; нужны пилоты на реальных камерах.
- **Платформа без архитектуры** — преждевременная микросервисизация убьёт скорость; сначала **чёткие границы модулей** в одном процессе.

---

## 6. Parity-Recorder-1.0 — чеклист среза продукта

Измеримые критерии «коробка регистратора готова к полю» (обновляйте статус при закрытии):

1. **Запись и ретеншн:** непрерывная запись по всем включённым камерам, ротация сегментов, eviction при заполнении диска — без ручного вмешательства 72 ч на эталонном железе.
2. **Режим записи:** поддерживаются `continuous`, `motion` и **`hybrid`** (гибрид требует `enable_motion` для маркировки); значение персистится в БД и в UI.
3. **Live и архив:** HLS/просмотр сегментов, фильтр по камере и интервалу; ошибки 401/403 понятны в UI.
4. **RBAC:** Viewer не меняет камеры; Operator ack событий; Admin — пользователи и конфиг.
5. **Лицензия:** лимит каналов соблюдается при добавлении камеры; статус лицензии виден в System/Dashboard.
6. **Уведомления:** минимум один канал (Telegram или webhook) доставляет тест и реальное motion-событие.
7. **Обновление и откат:** задокументированный порядок — [RUNBOOK.md](RUNBOOK.md) §2; бэкап `nvr.db` перед обновлением.
8. **Мониторинг:** `/api/v1/health/ready`, `/metrics` на loopback (в т.ч. `nvr_analytics_jobs_*`, `nvr_analytics_queue_dropped_total`); nginx + TLS для UI.
9. **PRE_FIELD:** пройден чеклист [PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md) на целевом регистраторе (дата прогона в документе).
10. **Нагрузка:** зафиксирован профиль «каналы × разрешение» без OOM и без роста задержки декодера выше порога — см. шаблон в [LOADTEST_SLO.md](LOADTEST_SLO.md); значения порогов записать в отчёт прогона.

---

## 7. Рекомендуемый следующий шаг

1. Зафиксировать **целевой срез «Parity-1»** на уровне продукта (только регистратор? + аналитика базового уровня?).
2. Вести [TRASSIR_PARITY_ROADMAP.md](TRASSIR_PARITY_ROADMAP.md) как живой документ: даты релизов, закрытые строки фаз, ссылки на PR.
3. Не расширять **Фазу D**, пока не закрыта **Фаза A** по чеклисту [PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md) на эталонном железе.

---

*Документ подготовлен как результат аудита разрывов; при изменении стратегии продукта обновите разделы 1 и 3.*
