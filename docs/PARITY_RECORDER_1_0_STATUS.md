# Parity-Recorder-1.0 — статус и связь с полем

Зеркало критериев из [TRASSIR_PARITY_ROADMAP.md](TRASSIR_PARITY_ROADMAP.md) §6. Отметки ниже — **для репозитория и CI**; финальный go/no-go требует прогона [PRE_FIELD_AUDIT_2026-05-13.md](PRE_FIELD_AUDIT_2026-05-13.md) на целевом железе.

| # | Критерий | Репозиторий / CI | Поле (PRE_FIELD) |
|---|-----------|------------------|------------------|
| 1 | Запись и ретеншн 72 ч | Код `ArchiveManager`, `CameraPipeline` — зафиксировано в roadmap | [ ] |
| 2 | `continuous` / `motion` + персист в БД/UI | Есть | [ ] |
| 3 | Live и архив, понятные 401/403 | HLS, маршруты архива, UI | [ ] |
| 4 | RBAC | Роли в API и [App.tsx](../webui/src/App.tsx) | [ ] |
| 5 | Лимит каналов лицензии | `LicenseGate`, UI | [ ] |
| 6 | Уведомления тест + motion | Каналы + шина; см. also `motion.detected` на EventBus | [ ] |
| 7 | Runbook обновление/откат | [RUNBOOK.md](RUNBOOK.md) | [ ] |
| 8 | `/health/ready`, `/metrics` | [RouteInfra.cpp](../src/api/RouteInfra.cpp) | [ ] |
| 9 | PRE_FIELD чеклист | Этот файл + PRE_FIELD doc | [ ] |
| 10 | Нагрузка каналы×разрешение | Шаблон [LOADTEST_SLO.md](LOADTEST_SLO.md), лог прогона [SLO_RUN_LOG_TEMPLATE.md](SLO_RUN_LOG_TEMPLATE.md) | [ ] |

**Последнее обновление документа (репозиторий):** 2026-05-13 — добавлены шаблоны SLO/PRE_FIELD cross-links и шина `motion.detected` для аналитики/интеграций.
