# Дополнительные фрагменты nginx

Каталог `conf.d/` предназначен для **локальных переопределений** на площадке (дополнительные `location`, заголовки, ACL по IP).

Основной шаблон остаётся в [../nvr.conf](../nvr.conf). При установке `.deb` можно положить сюда файлы `*.conf`, не изменяя пакетный `nvr.conf`, если ваш дистрибутив nginx подключает `include /etc/nginx/conf.d/*.conf;` (проверьте `nginx.conf`).

Рекомендации:

- не дублировать `listen`/`server_name` из основного сервера без понимания порядка include;
- для archive X-Accel см. [../snippets/nvr-prototype-archive-accel.conf](../snippets/nvr-prototype-archive-accel.conf).
