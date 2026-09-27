# OrangeM — мессенджер на C++

Полноценный мессенджер: серверная часть на **C++17** (свой HTTP/1.1 + WebSocket сервер, SQLite, OpenSSL)
и веб-клиент (SPA без сборки). Оранжево-синяя тема, логотип из папки проекта.

* **Публичный адрес:** http://213.108.1.226:8080
* **Админ:** логин `admin`, пароль `Orangem-Admin-9f3Kx2` (лежит в `/opt/orangem/orangem.env`)
* Сервер: Ubuntu 24.04, systemd-сервис `orangem`, каталог `/opt/orangem`

## Возможности

| Раздел | Что сделано |
|---|---|
| **Регистрация и вход (4 метода)** | по почте, по номеру телефона (пароль или одноразовый код), по флеш-ключу (файл сессии), через приложение-аутентификатор (TOTP, RFC 6238 + QR) |
| **Orange ID** | уникальный `OM-XXXXXXXX` + короткий номер + `@username`, единый идентификатор аккаунта во всех продуктах, поиск по нему |
| **Настройки** | профиль (аватар, баннер, имя, био, режим присутствия), конфиденциальность (кто пишет, кто видит истории/время/онлайн, поиск по телефону/почте, галочки прочтения), безопасность (пароль, 2FA, сессии, флеш-ключи, журнал), внешний вид (акцент, компакт, язык), админка |
| **Лента** | посты с заголовком/текстом/медиа, лайки, комментарии, тренды-хештеги, бесконечная прокрутка, поиск по хештегам и названиям |
| **Истории** | публикация фото/видео/текста, обводка вокруг аватарки, полноэкранный просмотр с прогресс-барами, отметки просмотра, список зрителей, автоудаление через 24 ч |
| **Личные сообщения** | личные и групповые чаты, вложения, ответы, реакции, редактирование/удаление, индикатор набора, галочки прочтения, realtime через WebSocket, поиск по ID чата/юзернейму/почте/Orange ID |
| **Сообщества** | группы и каналы, роли (владелец/админ/участник), публичные и закрытые, инвайт-коды, собственный чат и лента публикаций, поиск |
| **Прочее** | уведомления, блокировки, подписки, загрузка файлов, журнал флеш-ключей, демо-режим доставки кодов |

## Структура

```
server/
  src/            C++17: json, util, crypto, db, http(+WebSocket), api_*.cpp, main
  web/            веб-клиент: index.html, css/style.css, js/*.js, assets/logo.jpg
  Makefile        сборка: make -j
  web/SPEC.md     контракт API и модулей клиента
deploy/
  orangem.service systemd-юнит
  nginx-orangem.conf  reverse-proxy на :80 (когда порт свободен)
  deploy.sh       установка на сервер
tests/
  e2e.sh          65 проверок REST API
tools/
  rsh.js          SSH-хелпер для деплоя (парольный вход)
  uitest.js       41 проверка отрисовки UI в jsdom
  uitest2.js      28 интерактивных проверок UI (регистрация, 2FA, realtime, флеш-ключ…)
  wstest.js       14 проверок WebSocket
  qrtest/         проверка QR-энкодера декодером jsQR (все 40 версий)
```

## Сборка и запуск

```bash
cd server && make -j            # g++ -std=c++17, нужны libsqlite3-dev, libssl-dev
./orangem --data ./data --web ./web --port 8080
```

Параметры окружения: `ORANGEM_HOST`, `ORANGEM_PORT`, `ORANGEM_DATA`, `ORANGEM_WEB`,
`ORANGEM_DEV_CODES`, `ORANGEM_PUBLIC_URL`, `ORANGEM_ADMIN_USER`, `ORANGEM_ADMIN_PASS`,
`ORANGEM_SMTP_HOST/USER/PASS/FROM` (для реальной отправки писем через curl+SMTP).

## Управление на сервере

```bash
systemctl status orangem          # состояние
systemctl restart orangem         # перезапуск
tail -f /var/log/orangem.log      # логи
journalctl -u orangem -n 50       # логи через journal
```

## Флеш-ключ (файл сессии)

Файл `*.omkey` — текстовый контейнер с AES-256-GCM шифротекстом:

```
-----BEGIN ORANGEM SESSION KEY-----
uuid: <идентификатор ключа>
orange_id: OM-XXXXXXXX
owner: <username>
rotations: N
payload: <base64(AES-256-GCM(payload))>
-----END ORANGEM SESSION KEY-----
```

* при каждом входе сервер проверяет секрет внутри файла и **сразу перешифровывает** файл новым ключом;
* клиент записывает обновлённый файл обратно на носитель (File System Access API) — то есть данные
  на флешке обновляются в реальном времени;
* раз в ~5 минут, пока сессия активна, сервер по WebSocket присылает новую версию файла
  (событие `session_file.update`), и клиент перезаписывает его на носителе;
* отозвать ключ можно в «Настройки → Устройства и флеш-ключи», там же журнал входов/ротаций.

## Одноразовые коды

SMTP-провайдер не настроен, поэтому коды подтверждения показываются прямо в интерфейсе с пометкой
«демо-режим доставки» и доступны админу в «Настройки → Админ → Журнал отправки». Чтобы включить
реальную отправку, заполните `ORANGEM_SMTP_*` в `/opt/orangem/orangem.env` и выставите
`ORANGEM_DEV_CODES=0`, затем `systemctl restart orangem`.

## Тесты

```bash
bash tests/e2e.sh http://127.0.0.1:8080     # на сервере: 65 проверок API
node tools/wstest.js                        # realtime: 14 проверок
node tools/uitest.js                        # UI-отрисовка: 41 проверка
node tools/uitest2.js                       # UI-интерактив: 28 проверок
```

Проверка без VDS (всё локально):

```powershell
node tools\mock-server.js                    # мок REST + WebSocket на 127.0.0.1:8099
desktop\orangem.exe --selftest --server=http://127.0.0.1:8099   # 23 проверки клиента Windows
powershell -File desktop\gui-smoke.ps1       # запуск окна входа + скриншот
powershell -File desktop\gui-demo.ps1        # отрисовка всех 5 разделов + скриншоты
node tools\webshots.js                       # 19 скриншотов веб-клиента (нужен Chrome с --remote-debugging-port=9222)
desktop\tests\ui_harness.cpp                 # стенд взаимодействия: 59 проверок
```

## Десктопный клиент для Windows (C++)

`desktop/` — нативное приложение Win32 (GDI/GDI+, всё owner-draw), без сторонних библиотек,
один исполняемый файл ~3,3 МБ (статическая линковка).

* вход: почта/телефон + пароль, флеш-ключ (`*.omkey`), код аутентификатора;
* чаты: список, переписка, отправка, вложения, realtime через WebSocket (те же события, что в веб-клиенте);
* лента: публикации, лайки, комментарии; сообщества: список, вступление, переход в чат;
* истории: список с обводкой непросмотренных и публикация текстовой истории;
* профиль: Orange ID, статистика, адрес сервера, состояние соединения;
* самотест без GUI: `orangem.exe --selftest --server=http://213.108.1.226:8080` (проверяет сеть, JSON,
  флеш-ключи, ленту, чаты и WebSocket и печатает PASS/FAIL).

Сборка:

```powershell
# локально портативным MinGW-w64 (D:\messenger\tools\mingw)
powershell -ExecutionPolicy Bypass -File desktop\build-windows.ps1
# или кросс-сборка на Linux
cd desktop && make
```

Дымовой тест интерфейса (запуск, проверка окна, скриншот): `powershell -File desktop\gui-smoke.ps1`.

## Порт 80

Порт 80 занят чужим процессом (`node /root/messenger/messenger/server.js` — старый демо-чат на socket.io).
OrangeM сейчас работает на :8080. Чтобы отдать ему 80-й порт:

```bash
systemctl stop orangem
pkill -f '/root/messenger/messenger/server.js'     # остановить старый процесс
sed -i 's/^ORANGEM_HOST=.*/ORANGEM_HOST=127.0.0.1/' /opt/orangem/orangem.env
systemctl start orangem
ln -sf /etc/nginx/sites-available/orangem /etc/nginx/sites-enabled/orangem
nginx -t && systemctl restart nginx
```

Конфиг nginx уже установлен (`/etc/nginx/sites-available/orangem`) и проксирует :80 → 127.0.0.1:8080
вместе с WebSocket. При появлении домена там же добавляется HTTPS (Let's Encrypt).
