# SLO / производительность (черновик)

Целевые ориентиры для **встроенного** HTTP API (`nvr_prototype` за nginx). Фактические метрики: `/metrics` (Prometheus).

| Поверхность | Индикатор | Цель |
|-------------|-----------|------|
| Liveness | `GET /healthz`, `GET /api/v1/health` | 200, p95 < 50 ms на loopback |
| Архив | `GET /api/v1/archive/segments` (лимит 100) | p95 < 300 ms при SSD и тёплом кэше ОС |
| Экспорт диапазона | `POST /api/v1/archive/export` | функционально ≤ 256 MiB; зависит от ffmpeg и диска |

Нагрузочный **смоук** без авторизации: `scripts/k6-nvr-smoke.js` (см. `docs/RUNBOOK.md`).
