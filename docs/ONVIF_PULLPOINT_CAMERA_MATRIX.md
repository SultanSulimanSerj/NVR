# Матрица камер для ONVIF PullPoint (ручная)

Перед полевым прогоном зафиксируйте для каждой модели:

| Камера | Прошивка | Events XAddr (если нестандартный) | CreatePullPointSubscription | PullMessages |
|--------|----------|-----------------------------------|-------------------------------|----------------|
| …      | …        |                                   | OK / FAIL                     | OK / FAIL      |

Вызов API: `POST /api/v1/cameras/{id}/onvif-events/pull-once` с телом `{"events_service_url":"..."}` при необходимости.
