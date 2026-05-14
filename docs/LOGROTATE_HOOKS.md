# Logrotate и хуки Python

Логи приложения задаются в `nvr.yaml` (`logging_file`). Для ротации без потери дескриптора используйте `copytruncate` **или** переоткрытие логов по сигналу (если добавите в бинарь `SIGHUP` для логгера — см. roadmap техдолга).

Пример `/etc/logrotate.d/nvr-prototype` (адаптируйте пути):

```
/var/log/nvr-prototype/*.log {
  daily
  rotate 14
  compress
  delaycompress
  missingok
  notifempty
  copytruncate
}
```

Хуки `python` пишут в stdout/stderr контейнера/юнита — ротация через journald или тот же каталог, если перенаправление настроено в systemd `StandardOutput=append:`.
