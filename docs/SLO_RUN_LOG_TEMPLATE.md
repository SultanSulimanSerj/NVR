# Шаблон отчёта SLO / нагрузочного прогона

Используйте после прогона [scripts/loadtest/k6-health.js](../scripts/loadtest/k6-health.js) и/или [scripts/loadtest/k6-smoke.js](../scripts/loadtest/k6-smoke.js). Матрица порогов — [LOADTEST_SLO.md](LOADTEST_SLO.md).

| Поле | Значение |
|------|----------|
| Дата (UTC) | |
| Commit / версия пакета | |
| Железо (CPU, RAM, диск архива) | |
| Число камер × разрешение × FPS | |
| Переменные k6 (`NVR_LOADTEST_BASE_URL`, …) | |
| Итог health (pass/fail) | |
| Итог smoke (pass/fail / N/A) | |
| Пик CPU % (средний / max) | |
| Дропы кадров (из `/metrics` или UI stream-stats) | |
| Задержка декодера / lag (порог из LOADTEST_SLO) | |
| Примечания / регрессии | |

Сохраняйте как `docs/field_reports/SLO_<YYYYMMDD>_<hostname>.md` (каталог можно создать на стенде) или приложение к тикету релиза.
