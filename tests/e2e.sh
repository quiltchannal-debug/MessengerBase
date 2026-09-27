#!/usr/bin/env bash
# OrangeM — сквозной тест API (запускается на сервере)
# Использование: bash e2e.sh [http://127.0.0.1:8080]
BASE="${1:-http://127.0.0.1:8080}"
PASS=0; FAIL=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

jget() { python3 -c "
import sys,json
d=json.load(sys.stdin)
p='$1'.split('.')
for k in p:
    if k=='': continue
    if isinstance(d,list): d=d[int(k)]
    else: d=d.get(k)
    if d is None: print(''); sys.exit(0)
print('True' if d is True else ('False' if d is False else d))
"; }

ok()   { PASS=$((PASS+1)); printf '  \033[32mPASS\033[0m %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  \033[31mFAIL\033[0m %s -- %s\n' "$1" "$2"; }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "ожидалось '$3', получено '$2'"; fi; }
checkne(){ if [ "$2" != "$3" ]; then ok "$1"; else bad "$1" "значение не должно быть '$3'"; fi; }

api() { # method path token body
  local m="$1" p="$2" t="$3" b="$4"
  local args=(-s -X "$m" "$BASE$p" -H 'Content-Type: application/json')
  [ -n "$t" ] && args+=(-H "X-Orange-Token: $t")
  [ -n "$b" ] && args+=(-d "$b")
  curl "${args[@]}"
}

echo "== OrangeM e2e =="
TS=$(date +%s)
U1="omtest1_$TS"; U2="omtest2_$TS"; E1="om1_$TS@example.com"; P1="+7999$(printf '%07d' $((TS % 10000000)))"; E2="om2_$TS@example.com"

echo "-- health"
H=$(api GET /api/health "" "")
check "health ok" "$(echo "$H" | jget ok)" "True"

echo "-- регистрация по почте"
R=$(api POST /api/auth/register "" "{\"method\":\"email\",\"email\":\"$E1\",\"username\":\"$U1\",\"display_name\":\"Тест Один\",\"password\":\"SuperPass123\"}")
check "register pending" "$(echo "$R" | jget pending)" "True"
CODE=$(echo "$R" | jget dev_code)
checkne "dev_code выдан" "$CODE" ""
R=$(api POST /api/auth/register/verify "" "{\"target\":\"$E1\",\"code\":\"$CODE\"}")
T1=$(echo "$R" | jget token); OID1=$(echo "$R" | jget user.orange_id); SID1=$(echo "$R" | jget user.short_id); ID1=$(echo "$R" | jget user.id)
checkne "токен пользователя 1" "$T1" ""
check "orange id" "$(echo "$OID1" | cut -c1-3)" "OM-"
check "почта подтверждена" "$(echo "$R" | jget user.email_verified)" "True"

echo "-- регистрация по телефону"
R=$(api POST /api/auth/register "" "{\"method\":\"phone\",\"phone\":\"$P1\",\"username\":\"$U2\",\"display_name\":\"Тест Два\",\"password\":\"SuperPass456\"}")
CODE=$(echo "$R" | jget dev_code)
R=$(api POST /api/auth/register/verify "" "{\"target\":\"$P1\",\"code\":\"$CODE\"}")
T2=$(echo "$R" | jget token); ID2=$(echo "$R" | jget user.id)
checkne "токен пользователя 2" "$T2" ""

echo "-- вход по паролю"
R=$(api POST /api/auth/login "" "{\"identifier\":\"$E1\",\"password\":\"SuperPass123\"}")
checkne "login email" "$(echo "$R" | jget token)" ""
R=$(api POST /api/auth/login "" "{\"identifier\":\"$P1\",\"password\":\"SuperPass456\"}")
checkne "login phone" "$(echo "$R" | jget token)" ""
R=$(api POST /api/auth/login "" "{\"identifier\":\"$U1\",\"password\":\"wrongpass\"}")
check "неверный пароль" "$(echo "$R" | jget ok)" "False"

echo "-- вход по одноразовому коду"
R=$(api POST /api/auth/otp/request "" "{\"identifier\":\"$E1\"}")
CODE=$(echo "$R" | jget dev_code)
R=$(api POST /api/auth/otp/verify "" "{\"identifier\":\"$E1\",\"code\":\"$CODE\"}")
checkne "otp login" "$(echo "$R" | jget token)" ""

echo "-- профиль и поиск"
R=$(api GET /api/me "$T1" "")
check "me username" "$(echo "$R" | jget user.username)" "$U1"
R=$(api PATCH /api/users/me "$T1" '{"display_name":"Тест Первый","bio":"привет OrangeM"}')
check "обновление профиля" "$(echo "$R" | jget user.display_name)" "Тест Первый"
R=$(api GET "/api/users/search?q=$U2" "$T1" "")
check "поиск по юзернейму" "$(echo "$R" | jget users.0.username)" "$U2"
R=$(api GET "/api/users/search?q=$P1" "$T1" "")
check "поиск по телефону" "$(echo "$R" | jget users.0.username)" "$U2"
R=$(api GET "/api/users/search?q=$SID1" "$T2" "")
check "поиск по короткому ID" "$(echo "$R" | jget users.0.username)" "$U1"
R=$(api GET "/api/users/search?q=$(echo $OID1)" "$T2" "")
check "поиск по Orange ID" "$(echo "$R" | jget users.0.orange_id)" "$OID1"
R=$(api POST "/api/users/$ID2/follow" "$T1" "")
check "подписка" "$(echo "$R" | jget following)" "True"

echo "-- лента"
R=$(api POST /api/posts "$T1" '{"title":"Привет OrangeM","body":"Первая публикация #orangem #тест","visibility":"public"}')
PID=$(echo "$R" | jget post.id)
checkne "пост создан" "$PID" ""
check "хештег извлечён" "$(echo "$R" | jget post.tags.0)" "orangem"
R=$(api GET "/api/feed?scope=global&limit=10" "$T2" "")
check "пост в ленте" "$(echo "$R" | jget posts.0.id)" "$PID"
R=$(api GET "/api/feed?tag=orangem" "$T2" "")
check "поиск по хештегу" "$(echo "$R" | jget posts.0.id)" "$PID"
R=$(api GET "/api/feed?q=Привет" "$T2" "")
check "поиск по названию" "$(echo "$R" | jget posts.0.id)" "$PID"
R=$(api GET /api/feed/trends "$T2" "")
checkne "тренды" "$(echo "$R" | jget trends.0.tag)" ""
R=$(api POST "/api/posts/$PID/like" "$T2" "")
check "лайк" "$(echo "$R" | jget liked)" "True"
R=$(api POST "/api/posts/$PID/comments" "$T2" '{"body":"Отличный пост!"}')
check "комментарий" "$(echo "$R" | jget comments)" "1"
R=$(api GET "/api/posts/$PID/comments" "$T1" "")
check "чтение комментариев" "$(echo "$R" | jget comments.0.body)" "Отличный пост!"
R=$(api GET "/api/search?scope=all&q=orangem" "$T2" "")
checkne "общий поиск" "$(echo "$R" | jget posts.0.id)" ""

echo "-- истории"
R=$(api POST "/api/users/$ID1/follow" "$T2" "")
check "обратная подписка" "$(echo "$R" | jget following)" "True"
R=$(api POST /api/stories "$T1" '{"kind":"text","caption":"Моя первая история","background":"#ff7a1a","privacy":"everyone"}')
SID=$(echo "$R" | jget story.id)
checkne "история создана" "$SID" ""
R=$(api GET /api/stories "$T2" "")
check "история видна подписчику" "$(echo "$R" | jget groups.0.items.0.id)" "$SID"
R=$(api POST "/api/stories/$SID/view" "$T2" "")
check "просмотр отмечен" "$(echo "$R" | jget views)" "1"

echo "-- чаты"
R=$(api POST /api/chats "$T1" "{\"kind\":\"dm\",\"user_id\":$ID2}")
CID=$(echo "$R" | jget chat.id)
checkne "личный чат создан" "$CID" ""
R=$(api POST "/api/chats/$CID/messages" "$T1" '{"body":"Привет! Это OrangeM"}')
MID=$(echo "$R" | jget message.id)
checkne "сообщение отправлено" "$MID" ""
R=$(api GET "/api/chats/$CID/messages" "$T2" "")
check "сообщение получено" "$(echo "$R" | jget messages.0.body)" "Привет! Это OrangeM"
R=$(api POST "/api/chats/$CID/read" "$T2" "{\"message_id\":$MID}")
check "прочтение" "$(echo "$R" | jget last_read)" "$MID"
R=$(api POST "/api/messages/$MID/reactions" "$T2" '{"emoji":"🔥"}')
check "реакция" "$(echo "$R" | jget ok)" "True"
R=$(api GET "/api/chats/search?q=$U2" "$T1" "")
checkne "поиск чатов по юзернейму" "$(echo "$R" | jget users.0.username)" ""
R=$(api GET "/api/chats/search?q=$CID" "$T1" "")
check "поиск чатов по ID" "$(echo "$R" | jget chats.0.id)" "$CID"
R=$(api POST /api/chats "$T1" "{\"kind\":\"group\",\"title\":\"Тестовая группа\",\"members\":[$ID2]}")
GID=$(echo "$R" | jget chat.id)
checkne "группа создана" "$GID" ""
R=$(api GET /api/chats "$T2" "")
checkne "список чатов" "$(echo "$R" | jget chats.0.id)" ""

echo "-- сообщества"
R=$(api POST /api/communities "$T1" '{"name":"OrangeM Клуб","description":"Сообщество тестировщиков","kind":"group","is_public":true}')
CMID=$(echo "$R" | jget community.id); CMOID=$(echo "$R" | jget community.orange_id)
checkne "сообщество создано" "$CMID" ""
R=$(api POST "/api/communities/$CMID/join" "$T2" "")
check "вступление" "$(echo "$R" | jget community.is_member)" "True"
R=$(api POST /api/posts "$T1" "{\"title\":\"Пост в сообществе\",\"body\":\"Привет участникам\",\"community_id\":$CMID}")
check "пост в сообществе" "$(echo "$R" | jget post.community_id)" "$CMID"
R=$(api GET "/api/feed?scope=community&community=$CMID" "$T2" "")
check "лента сообщества" "$(echo "$R" | jget posts.0.title)" "Пост в сообществе"
R=$(api GET "/api/communities?q=OrangeM" "$T2" "")
checkne "поиск сообществ" "$(echo "$R" | jget communities.0.id)" ""
R=$(api GET "/api/communities/$CMOID" "$T2" "")
checkne "сообщество по Orange ID" "$(echo "$R" | jget community.id)" ""
R=$(api POST "/api/communities/$CMID/invites" "$T1" '{"max_uses":5}')
INV=$(echo "$R" | jget code)
checkne "приглашение" "$INV" ""

echo "-- TOTP (аутентификатор)"
R=$(api POST /api/auth/totp/setup "$T1" "")
SECRET=$(echo "$R" | jget secret)
checkne "секрет TOTP" "$SECRET" ""
TOTP=$(python3 -c "
import base64,hmac,hashlib,struct,time
key=base64.b32decode('$SECRET')
c=int(time.time())//30
h=hmac.new(key,struct.pack('>Q',c),hashlib.sha1).digest()
o=h[19]&15
print(str((struct.unpack('>I',h[o:o+4])[0]&0x7fffffff)%1000000).zfill(6))")
R=$(api POST /api/auth/totp/enable "$T1" "{\"code\":\"$TOTP\"}")
check "TOTP включён" "$(echo "$R" | jget totp_enabled)" "True"
R=$(api POST /api/auth/login "" "{\"identifier\":\"$E1\",\"password\":\"SuperPass123\"}")
CH=$(echo "$R" | jget challenge)
check "запрос 2FA при входе" "$(echo "$R" | jget need_totp)" "True"
TOTP=$(python3 -c "
import base64,hmac,hashlib,struct,time
key=base64.b32decode('$SECRET')
c=int(time.time())//30
h=hmac.new(key,struct.pack('>Q',c),hashlib.sha1).digest()
o=h[19]&15
print(str((struct.unpack('>I',h[o:o+4])[0]&0x7fffffff)%1000000).zfill(6))")
R=$(api POST /api/auth/login/totp "" "{\"challenge\":\"$CH\",\"code\":\"$TOTP\"}")
checkne "вход по коду из аутентификатора" "$(echo "$R" | jget token)" ""

echo "-- флеш-ключ"
R=$(api POST /api/auth/session-file/create "$T1" '{"label":"Тестовая флешка"}')
KF=$(echo "$R" | jget content)
checkne "ключ сгенерирован" "$KF" ""
UUID=$(echo "$KF" | sed -n 's/^uuid: \(.*\)$/\1/p')
R=$(python3 -c "
import json,sys
print(json.dumps({'content': sys.stdin.read()}))" <<< "$KF" > "$TMP/kf.json")
LOGIN=$(curl -s -X POST "$BASE/api/auth/session-file/login" -H 'Content-Type: application/json' --data @"$TMP/kf.json")
checkne "вход по флеш-ключу" "$(echo "$LOGIN" | jget token)" ""
NEW=$(echo "$LOGIN" | jget rotated_content)
checkne "ключ перешифрован" "$NEW" "$KF"
R=$(api POST /api/auth/session-file/login "" "{\"content\":$(python3 -c "import json,sys;print(json.dumps(sys.stdin.read()))" <<< "$KF")}")
check "старый ключ отклонён (ротация)" "$(echo "$R" | jget ok)" "False"
R=$(api GET /api/auth/session-files "$T1" "")
check "список ключей" "$(echo "$R" | jget files.0.rotations)" "1"
R=$(api GET /api/auth/session-files/log "$T1" "")
checkne "журнал ключа" "$(echo "$R" | jget events.0.action)" ""

echo "-- сессии и приватность"
R=$(api GET /api/auth/sessions "$T1" "")
checkne "список сессий" "$(echo "$R" | jget sessions.0.kind)" ""
R=$(api PATCH /api/users/me/privacy "$T1" '{"dm":"contacts","last_seen":"nobody","read_receipts":false}')
check "приватность обновлена" "$(echo "$R" | jget user.privacy.dm)" "contacts"
R=$(api PATCH /api/users/me/settings "$T1" '{"settings":{"accent":"orange","compact":true}}')
check "настройки сохранены" "$(echo "$R" | jget settings.compact)" "True"
R=$(api POST /api/auth/password/change "$T1" '{"old_password":"SuperPass123","new_password":"NewSuperPass123"}')
check "смена пароля" "$(echo "$R" | jget ok)" "True"
R=$(api POST /api/auth/login "" "{\"identifier\":\"$U1\",\"password\":\"NewSuperPass123\"}")
check "вход с новым паролем" "$(echo "$R" | jget need_totp)" "True"

echo "-- уведомления и админ"
R=$(api GET /api/notifications "$T1" "")
checkne "уведомления" "$(echo "$R" | jget unread)" ""
R=$(api GET /api/admin/stats "$T1" "")
check "не-админ отсечён" "$(echo "$R" | jget ok)" "False"

echo
echo "== ИТОГО: PASS=$PASS FAIL=$FAIL =="
[ "$FAIL" -eq 0 ]
