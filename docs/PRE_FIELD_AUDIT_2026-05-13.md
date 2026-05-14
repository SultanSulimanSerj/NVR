# Аудит перед установкой на видеорегистратор (поле)

**Дата шаблона:** 2026-05-13  
**Ответственный за прогон:** _________________  
**Версия прошивки / commit:** _________________  
**Железо (модель CPU, RAM, диски):** _________________

Этот документ фиксирует проверки перед заливкой на регистратор. Отмечайте `[x]` только после фактического прохода на целевой машине или стенде.

Связанные материалы: [AUDIT_2026-05.md](AUDIT_2026-05.md), [PLAN_RECORDER_ANALYTICS_LICENSE.md](PLAN_RECORDER_ANALYTICS_LICENSE.md), [LICENSE_TICKET.md](LICENSE_TICKET.md), [openapi.yaml](openapi.yaml), [RUNBOOK.md](RUNBOOK.md), [LOADTEST_SLO.md](LOADTEST_SLO.md), [TRASSIR_PARITY_ROADMAP.md](TRASSIR_PARITY_ROADMAP.md) §6 (Parity-Recorder-1.0), [PARITY_RECORDER_1_0_STATUS.md](PARITY_RECORDER_1_0_STATUS.md), [SLO_RUN_LOG_TEMPLATE.md](SLO_RUN_LOG_TEMPLATE.md), [TRASSIR_HORIZON1_BACKLOG.md](TRASSIR_HORIZON1_BACKLOG.md), [TRASSIR_HORIZON2_ANALYTICS.md](TRASSIR_HORIZON2_ANALYTICS.md), [TRASSIR_HORIZON3_CORP_UI.md](TRASSIR_HORIZON3_CORP_UI.md), [ARCHITECTURE_TRASSIR_PLATFORM.md](ARCHITECTURE_TRASSIR_PLATFORM.md).

**Дата последней синхронизации шаблона с кодом (репозиторий):** 2026-05-13.

---

## P0 — обязательно перед выездом

| # | Проверка | Статус |
|---|-----------|--------|
| 1 | `cmake -DNVR_BUILD_TESTS=ON && cmake --build && ctest` | [ ] |
| 2 | `cd webui && npm ci && npm run build` | [ ] |
| 3 | First-run: `setup.token`, finalize, смена пароля, вход | [ ] |
| 4 | Роли: admin / operator / viewer — доступ к разделам согласно [App.tsx](../webui/src/App.tsx) | [ ] |
| 5 | Камеры: добавление до лимита лицензии/trial; попытка сверх — **403** и понятное сообщение в UI | [ ] |
| 6 | Live (HLS) минимум по одной камере | [ ] |
| 7 | Архив: список сегментов, воспроизведение | [ ] |
| 8 | Перезагрузка `nvr-prototype`, `journalctl -u nvr-prototype` без критических ошибок | [ ] |
| 9 | Диск архива: `statvfs`/квота не уходит в аварию при тестовой записи | [ ] |

---

## P1 — желательно

| # | Проверка | Статус |
|---|-----------|--------|
| 10 | Ручной регресс по матрице ниже (все маршруты) | [ ] |
| 11 | k6: [scripts/loadtest/k6-health.js](../scripts/loadtest/k6-health.js) или полный smoke — **дата последнего прогона:** ______; матрица SLO — [docs/LOADTEST_SLO.md](LOADTEST_SLO.md) | [ ] |
| 12 | Playwright e2e на стенде (см. README, `vars.NVR_E2E_BASE_URL`) — **дата / билд:** ______ или N/A | [ ] |
| 13 | Экран «Лицензия» в UI (Система) сверен с `GET /api/v1/license` | [ ] |
| 14 | nginx + TLS + `nvr_sync_nginx_archive_accel.sh` после смены `archive.root_path` | [ ] |

---

## P2 — после поля / итерация

| # | Проверка | Статус |
|---|-----------|--------|
| 15 | Нагрузка N камер × целевое разрешение (CPU, дропы кадров) | [ ] |
| 16 | Ложные срабатывания motion — сбор замечаний | [ ] |
| 17 | Матрица «модуль аналитики ↔ бит лицензии» (L4 продукта) | [ ] |

---

## Матрица ручного регресса UI

Маршруты из [webui/src/App.tsx](../webui/src/App.tsx). Для каждой строки: открыть URL, убедиться что страница грузится без ошибки в консоли, ключевое действие выполняется.

| Маршрут | Страница | Минимальная роль | Действие для проверки |
|---------|-----------|------------------|------------------------|
| `/login` | Login | — | Вход, неверный пароль |
| `/setup` | Setup | — | Только first-run; finalize |
| `/local-dashboard` | LocalDashboard / киоск | — | Обмен токена, редирект |
| `/` | Dashboard | viewer+ | KPI, события, блок лицензии (если есть) |
| `/live` | Live | viewer+ | Сетка, переход `/live/:id` |
| `/archive` | Archive | viewer+ | Фильтр, play |
| `/events` | Events | viewer+ | Список, ack |
| `/cameras` | Cameras | operator+ | Добавить/редактировать, лимит лицензии |
| `/motion` | Motion | admin | Сохранение ROI/настроек |
| `/ai` | Ai | operator+ | Список моделей/лиц (если включено) |
| `/storage` | Storage | viewer+ | Диски / usage |
| `/notifications` | Notifications | admin | Список каналов |
| `/notifications/dlq` | Dlq | admin | Список DLQ |
| `/users` | Users | admin | Список, добавление |
| `/audit` | Audit | admin | Записи журнала |
| `/system` | System | admin | Вкладки info/net/time/services/logs/**license**/kiosk/power |

**Киоск:** отдельно проверить сценарий «только просмотр» на HDMI: нет лишних админских действий в `LocalDashboard` при роли viewer.

---

## CI / стенд: e2e и нагрузка

| Инструмент | Где настроено | Запись о последнем прогоне |
|------------|---------------|------------------------------|
| Playwright | [.github/workflows/ci.yml](../.github/workflows/ci.yml) — job `e2e` при `vars.NVR_E2E_BASE_URL` + `secrets.NVR_E2E_ADMIN_PASSWORD` | Дата: ______ Результат: ______ |
| k6 health | [.github/workflows/loadtest.yml](../.github/workflows/loadtest.yml) — `workflow_dispatch`, `vars.NVR_LOADTEST_BASE_URL` | Дата: ______ См. [LOADTEST_SLO.md](LOADTEST_SLO.md) |
| k6 smoke (логин+HLS) | [scripts/loadtest/k6-smoke.js](../scripts/loadtest/k6-smoke.js) | Дата: ______ |

Если стенда нет — явно написать **N/A** и сослаться на только ручной регресс (строка 10).

---

## Замечания по полю (свободная форма)

_Пишите сюда дефекты, не блокирующие отгрузку, и идеи улучшения UX._

1. _________________________________________________
2. _________________________________________________

---

## Итог go / no-go

- [ ] **GO** — все P0 закрыты, критичные P1 задокументированы или закрыты.  
- [ ] **NO-GO** — причина: _________________________________________________

Подпись: _________________ Дата: ______
