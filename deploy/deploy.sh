#!/usr/bin/env bash
# OrangeM — установка на сервер (Ubuntu 24.04): пользователь, systemd, nginx, firewall.
set -euo pipefail

APP_DIR=/opt/orangem
ADMIN_PASS="${ORANGEM_ADMIN_PASS:-Orangem-Admin-9f3Kx2}"
PUBLIC_URL="${ORANGEM_PUBLIC_URL:-http://213.108.1.226}"

echo "== 1. пользователь и каталоги =="
id -u orangem >/dev/null 2>&1 || useradd --system --home "$APP_DIR" --shell /usr/sbin/nologin orangem
mkdir -p "$APP_DIR/data/uploads" "$APP_DIR/web" "$APP_DIR/tests" "$APP_DIR/server"
install -m 755 /tmp/_build/orangem "$APP_DIR/server/orangem"

echo "== 2. права =="
chown -R orangem:orangem "$APP_DIR/data"
chmod 750 "$APP_DIR/data" "$APP_DIR/data/uploads"
chown -R root:root "$APP_DIR/server" "$APP_DIR/web" "$APP_DIR/tests"
chmod -R a+rX "$APP_DIR/web" "$APP_DIR/server/orangem" "$APP_DIR/tests"

echo "== 3. переменные окружения =="
cat > "$APP_DIR/orangem.env" <<EOF
ORANGEM_DEV_CODES=1
ORANGEM_ADMIN_USER=admin
ORANGEM_ADMIN_PASS=$ADMIN_PASS
ORANGEM_PUBLIC_URL=$PUBLIC_URL
ORANGEM_HOST=127.0.0.1
ORANGEM_PORT=8080
# Почта (необязательно). Пока не задано — коды показываются в интерфейсе (демо-режим).
#ORANGEM_SMTP_HOST=smtp.yandex.ru:465
#ORANGEM_SMTP_USER=you@yandex.ru
#ORANGEM_SMTP_PASS=app-password
#ORANGEM_SMTP_FROM=you@yandex.ru
EOF
chmod 600 "$APP_DIR/orangem.env"
chown root:root "$APP_DIR/orangem.env"

echo "== 4. systemd =="
install -m 644 /tmp/_build/orangem.service /etc/systemd/system/orangem.service
touch /var/log/orangem.log
chown orangem:orangem /var/log/orangem.log
systemctl daemon-reload
systemctl enable orangem >/dev/null 2>&1 || true
systemctl restart orangem
sleep 2

echo "== 5. nginx =="
install -m 644 /tmp/_build/nginx-orangem.conf /etc/nginx/sites-available/orangem
rm -f /etc/nginx/sites-enabled/default
ln -sf /etc/nginx/sites-available/orangem /etc/nginx/sites-enabled/orangem
nginx -t
systemctl enable nginx >/dev/null 2>&1 || true
systemctl restart nginx

echo "== 6. firewall =="
ufw allow OpenSSH >/dev/null 2>&1 || true
ufw allow 80/tcp >/dev/null 2>&1 || true
ufw allow 443/tcp >/dev/null 2>&1 || true
yes | ufw enable >/dev/null 2>&1 || true
ufw status | head -8

echo "== 7. проверка =="
sleep 1
systemctl --no-pager --lines=0 status orangem || true
curl -s -m 5 http://127.0.0.1:8080/api/health || echo "локальная проверка не прошла"
echo
curl -s -m 5 -o /dev/null -w 'nginx -> %{http_code}\n' http://127.0.0.1/ || true
echo "== готово =="
