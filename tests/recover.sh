#!/usr/bin/env bash
# Восстановление VDS после инцидента с wine: чистка, проверка диска, перезапуск OrangeM.
# Запуск: bash /opt/orangem/tests/recover.sh
set -uo pipefail

echo "== 1. снимаем зависшие процессы =="
pkill -9 -f wine 2>/dev/null || true
pkill -9 -f wineserver 2>/dev/null || true
pkill -9 -f Xvfb 2>/dev/null || true
pkill -9 -f xvfb-run 2>/dev/null || true
sleep 1
ps aux --sort=-%cpu | head -6

echo "== 2. удаляем wine и временные файлы =="
export DEBIAN_FRONTEND=noninteractive
apt-get purge -y -qq wine64 wine32 wine 2>/dev/null || true
apt-get autoremove -y -qq 2>/dev/null || true
rm -rf /root/.wine /root/.cache/wine /root/.local/share/applications/wine 2>/dev/null || true
rm -rf /tmp/.X11-unix /tmp/xvfb-run.* /tmp/_rsh_*.sh 2>/dev/null || true
rm -rf /opt/orangem/desktop/*.o 2>/dev/null || true
apt-get clean 2>/dev/null || true

echo "== 3. журналы =="
journalctl --vacuum-size=80M 2>/dev/null | tail -2 || true
rm -f /var/log/nginx/*.log.* 2>/dev/null || true

echo "== 4. диск =="
df -h / /tmp | sed -n '1,4p'
du -sh /opt/orangem /root/.wine /var/log 2>/dev/null | sort -h

echo "== 5. сервис OrangeM =="
systemctl restart orangem
sleep 2
systemctl is-active orangem
curl -s -m 5 http://127.0.0.1:8080/api/health || echo "сервер не отвечает"
echo

echo "== 6. быстрый e2e =="
bash /opt/orangem/tests/e2e.sh http://127.0.0.1:8080 2>&1 | tail -3
