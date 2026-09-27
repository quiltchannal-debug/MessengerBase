# OrangeM web-client contract (v1)

Фронтенд — чистый HTML/CSS/JS без сборки. Все модули лежат в `server/web/js/` и
подключаются `<script>`-тегами в `index.html` в порядке:
`api.js, ui.js, qrcode.js, realtime.js, auth.js, feed.js, stories.js, chats.js,
communities.js, profile.js, settings.js, app.js`.

Каждый модуль **обязан** следовать этому контракту. Никаких внешних библиотек и CDN —
только свой код (страница может работать без интернета).

## 0. Общие правила

* Глобальный объект `window.OM` уже существует (см. `api.js`, `ui.js`, `realtime.js`, `app.js`).
* Каждый модуль регистрирует свой раздел: `OM.views.<name> = { enter(params), leave() }`.
  `app.js` вызывает `enter(params)` при переходе и `leave()` при уходе.
* Никаких `alert()/prompt()/confirm()` — использовать `OM.toast`, `OM.modal`, `OM.confirm`, `OM.prompt`.
* Вся серверная часть — JSON, авторизация через заголовок `X-Orange-Token` (уже внутри `OM.api`).
* Ошибки API: `throw {status, message}` → перехватывать `try/catch` и показывать `OM.toast(e.message,'err')`.
* Весь пользовательский текст выводить через `OM.esc(...)` (или `OM.linkify` для текста постов/сообщений).
* Язык интерфейса — русский. Тема — оранжево-синяя (`css/style.css` уже написана, классы ниже).
* `node --check <файл>` должен проходить без ошибок.

## 1. Хелперы (уже реализованы)

```js
OM.api.get(path) / OM.api.post(path, body) / OM.api.patch / OM.api.del
OM.api.upload(file, kind)            // -> {url, kind, size}
OM.api.getToken() / setToken(t) / hasToken()
OM.state = { me, chats, activeChat, onlineUsers, unreadTotal, feedScope, feedQuery, feedTag, notifications }
OM.on(event, fn) / OM.emit(event, data)
OM.navigate(view, params)            // view = feed|chats|communities|stories|settings|profile|community
OM.views[name] = {enter, leave}
OM.toast(msg, 'ok'|'err', ms)
OM.modal({title, body, width, actions:[{label, cls, onClick(modal)}], footer:false, onClose}) -> {close(), body, root}
OM.confirm(text, onYes, yesLabel)
OM.prompt({title,label,placeholder,value,ok,hint}, async value => {...})
OM.esc(s) / OM.linkify(s) / OM.initials(u)
OM.avatar(user, {size:'xs'|'sm'|'lg'|'xl', ring:true, seen:true, online:true}) -> HTML строка
OM.timeAgo(ts) / OM.fmtTime(ts) / OM.fmtDateTime(ts) / OM.dayLabel(ts) / OM.onlineText(user)
OM.fmtNum(n) / OM.debounce(fn,ms) / OM.copy(text) / OM.download(name,content,mime)
OM.el(html) -> Element
OM.setMe(user) / OM.refreshMe() / OM.logout() / OM.showAuth() / OM.showApp()
OM.ws.connect() / send(obj) / typing(chatId) / read(chatId, msgId) / syncSessionFile(uuid, content)
OM.flashKey = { supported, save(content, suggestedName), pick(), rewrite(uuid, content), parseUuid(content) }
OM.updateChatBadge(n)
OM.idb.set(k,v)/get(k)/del(k)
```

События realtime (`OM.on('ws:<type>', fn)`): `ready`, `message.new`, `message.read`,
`message.edited`, `message.deleted`, `message.reaction`, `typing`, `presence`, `chat.new`,
`chat.updated`, `chat.left`, `post.new`, `story.new`, `notification`, `broadcast`,
`session_file.update`, `session_file.login`, `user.updated`, `ws:open`, `ws:close`.

События, которые модули должны эмитить для других модулей:
`chat-selected(chat)`, `unread-changed(n)`, `post-created(post)`, `story-open(userId)`,
`open-chat(chatId)`, `open-profile(userId)`, `open-community(id)`.

## 2. CSS-классы (готовы в `css/style.css`)

* Кнопки: `btn`, `btn primary`, `btn ghost`, `btn small`, `btn icon`, `btn danger`, `btn wide`
* Чипы/табы: `chip`, `chip active`, контейнер `head-chips`
* Формы: `field` (`label` внутри), `input`, `form-row`, `hint`, `err`, `okmsg`
* Карточки: `panel`, `card`, `card-grid`, `post-card`, `post-head`, `post-title`, `post-body`,
  `post-media`, `post-tags`, `tag`, `post-actions`, `post-action` (`on` = активная),
  `comment-list`, `comment`, `composer`
* Аватары: `avatar` + `sm|xs|lg|xl`, `avatar-wrap`, `avatar-ring`(`seen`), `presence-dot`(`online`)
* Чаты: `view-chats`, `chat-list-col`, `chat-list`, `chat-item`(`active`), `chat-head`,
  `chat-messages`, `msg`(`mine`|`deleted`|`msg-system`), `bub`, `au`, `tx`, `mt`, `msg-attach`,
  `msg-reply`, `reactions`, `reaction`(`mine`), `chat-typing`, `chat-input-row`, `day-sep`, `chat-badge`
* Сообщества: `comm-card`, `comm-banner`, `comm-body`, `comm-name`, `comm-desc`, `comm-meta`,
  `comm-hero` (внутри `.banner` и `.inner`), `member-row`, `badge`(`orange`,`grey`)
* Профиль: `profile-hero` (`.banner`, `.inner`), `kv`, `stat-row`, `stat`
* Настройки: `settings-wrap`, `settings-nav`, `settings-body`, `panel`, `desc`, `switch`, `lb`,
  `toggle`(`on`), `device-row`, `qr-box`, `secret`
* Истории: `stories-bar`, `story-item`, `story-add`, `stories-full`, `story-card`, `story-thumb`,
  `story-viewer`, `story-stage`, `story-progress`, `story-caption`, `story-top`, `story-nav`, `story-close`
* Прочее: `empty-state`, `empty-logo`, `hint`, `hidden`, `badge`

## 3. REST API (сервер `server/src/*.cpp`)

Все ответы содержат `{"ok":true, ...}` либо `{"ok":false,"error":"текст"}`.

### 3.1 Авторизация (модуль `auth.js`)

| Метод | Путь | Тело | Ответ |
|---|---|---|---|
| POST | `/api/auth/register` | `{method:'email'\|'phone', email?, phone?, username, display_name?, password}` | `{ok, pending:true, target, channel, expires_in, dev_code?}` |
| POST | `/api/auth/register/verify` | `{target, code}` | `{ok, token, kind, user}` |
| POST | `/api/auth/login` | `{identifier, password}` | `{ok, token, user}` либо `{ok, need_totp:true, challenge}` |
| POST | `/api/auth/login/totp` | `{challenge, code}` | `{ok, token, user}` |
| POST | `/api/auth/otp/request` | `{identifier}` | `{ok, target, channel, expires_in, dev_code?}` |
| POST | `/api/auth/otp/verify` | `{identifier, code}` | `{ok, token, user}` |
| POST | `/api/auth/session-file/create` (auth) | `{label}` | `{ok, file_name, content, hint}` |
| POST | `/api/auth/session-file/login` | `{content, device?}` | `{ok, token, user, rotated_content}` |
| POST | `/api/auth/session-file/sync` | `{content}` | `{ok, content}` |
| GET | `/api/auth/session-files` (auth) | — | `{ok, files:[{id,uuid,label,created_at,last_used,rotations,revoked}]}` |
| DELETE | `/api/auth/session-files/:id` (auth) | — | `{ok}` |
| GET | `/api/auth/session-files/log` (auth) | — | `{ok, events:[{action,uuid,ip,created_at}]}` |
| POST | `/api/auth/totp/setup` (auth) | — | `{ok, secret, uri, orange_id}` |
| POST | `/api/auth/totp/enable` (auth) | `{code}` | `{ok, totp_enabled:true}` |
| POST | `/api/auth/totp/disable` (auth) | `{code}` | `{ok, totp_enabled:false}` |
| GET | `/api/auth/sessions` (auth) | — | `{ok, sessions:[{id,kind,created_at,last_seen,ip,ua,device,current}]}` |
| DELETE | `/api/auth/sessions/:id` (auth) | — | `{ok}` |
| POST | `/api/auth/password/change` (auth) | `{old_password,new_password}` | `{ok}` |
| POST | `/api/auth/password/reset` | `{identifier}` | `{ok, target, channel, dev_code?}` |
| POST | `/api/auth/password/reset/verify` | `{identifier, code, new_password}` | `{ok, token, user}` |
| GET | `/api/auth/check?username=&email=&phone=` | — | `{ok, username_free?, email_free?, phone_free?}` |
| POST | `/api/auth/logout` | — | `{ok}` |
| POST | `/api/auth/logout/all` | — | `{ok}` |

`user` — объект профиля (см. 3.2). `dev_code` присутствует, когда на сервере включён
демо-режим доставки кодов — обязательно показывать его пользователю в интерфейсе
(подпись «код (демо-режим)») и давать кнопку «вставить».

**Требование к `auth.js`:** 4 метода входа должны быть явно представлены кнопками выбора
(`method-grid` + `method-btn`): «Почта», «Телефон», «Флеш-ключ», «Аутентификатор/код».
Регистрация — по почте или телефону с кодом подтверждения, с живой проверкой занятости
логина (`/api/auth/check`, debounce 400 мс). Вход по флеш-ключу: `OM.flashKey.pick()` →
`/api/auth/session-file/login` → сохранить токен → `OM.api.setToken` → **и** перезаписать
файл (`OM.flashKey.rewrite`, либо `OM.flashKey.save`, если дескриптор неизвестен) на
`rotated_content`, вывести подсказку про реальное время.

### 3.2 Пользователи и профиль (модули `profile.js`, `settings.js`, `auth.js`)

* `GET /api/me` → `{ok, user}` (полный: email, phone, privacy{}, settings{}, totp_enabled, session_files, orange_id, short_id)
* `GET /api/users/:id` (`id` = числовой id | `@username` без собаки | `OM-XXXX` | short_id) →
  `{ok, user}` либо `{ok, restricted:true, user:{...}}`
* `PATCH|POST /api/users/me` → `{display_name?, bio?, username?, avatar?, banner?, presence?}` → `{ok,user}`
* `PATCH|POST /api/users/me/privacy` → `{dm?, stories?, last_seen?, online?, profile?}` (значения
  `everyone|contacts|nobody`, для stories ещё `followers`) + `{read_receipts?, find_phone?, find_email?}` → `{ok,user}`
* `PATCH|POST /api/users/me/settings` → произвольный JSON-патч → `{ok, settings}`
* `GET /api/users/search?q=` → `{ok, users:[card]}`
* `POST /api/users/:id/follow` / `DELETE /api/users/:id/follow` → `{ok, following}`
* `GET /api/users/:id/follows?type=followers|following` → `{ok, users:[]}`
* `POST /api/users/:id/block` / `DELETE` ; `GET /api/users/me/blocked` → `{ok, users:[]}`
* `GET /api/notifications` → `{ok, notifications:[{id,kind,actor,text,entity,entity_id,read,created_at}], unread}`
* `POST /api/notifications/read` (`{ids?}`) → `{ok}`
* `GET /api/health` → статистика сервера
* `GET /api/admin/stats|outbox|users` + `POST /api/admin/users/:id/ban` + `POST /api/admin/broadcast`
  (только для `user.is_admin`)

**user card:** `{id, orange_id, short_id, username, display_name, avatar, banner, bio,
created_at, is_admin, presence:{online,last_seen,last_seen_text,hidden}, is_me, i_follow,
follows_me, blocked, is_contact, followers, following, posts, can_dm, email?, phone?}`

### 3.3 Лента, посты, истории (`feed.js`, `stories.js`)

* `GET /api/feed?scope=global|following|mine|user|community&cursor=&limit=&q=&tag=&user=&community=`
  → `{ok, posts:[post], next_cursor}`
* `GET /api/feed/trends` → `{ok, trends:[{tag,posts}]}`
* `POST /api/posts` `{title?, body?, media?, media_kind?, visibility?, community_id?}` → `{ok, post}`
* `GET|PATCH|DELETE /api/posts/:id` → `{ok, post}` / `{ok}`
* `POST|DELETE /api/posts/:id/like` → `{ok, likes, liked}`
* `GET /api/posts/:id/comments` → `{ok, comments:[{id,user,body,created_at,is_mine}]}`
* `POST /api/posts/:id/comments` `{body}` → `{ok, comment_id, comments}` ; `DELETE /api/comments/:id`
* `GET /api/search?scope=all|feed|users|chats&q=` → `{ok, posts:[], users:[]}`
* `GET /api/stories` → `{ok, groups:[{user, items:[story], has_unseen, count}]}`
* `POST /api/stories` `{kind:'image'|'text', media?, caption?, background?, privacy?}` → `{ok, story}`
* `POST /api/stories/:id/view` → `{ok, views}` ; `GET /api/stories/:id/views` → `{ok, viewers:[]}`
* `DELETE /api/stories/:id` → `{ok}`

**post:** `{id, author:{id,username,display_name,avatar,orange_id}, community?, title, body, media,
media_kind, visibility, likes, comments, views, created_at, edited_at, tags:[], liked, is_mine}`

### 3.4 Чаты (`chats.js`)

* `GET /api/chats?q=` → `{ok, chats:[chat]}` (сортировка: закреплённые, затем по `last_message_at`)
* `POST /api/chats` `{kind:'dm', user_id|identifier}` или `{kind:'group', title, members:[]}` → `{ok, chat}`
* `GET /api/chats/search?q=` → `{ok, chats:[], users:[]}` (по ID/юзернейму/почте/Orange ID)
* `GET /api/chats/:id` → `{ok, chat, members:[]}` ; `PATCH /api/chats/:id` `{title?,avatar?,description?,pinned?,muted?}`
* `POST /api/chats/:id/leave` ; `POST /api/chats/:id/members` `{identifier|user_id|members:[]}` ;
  `DELETE /api/chats/:id/members/:uid`
* `GET /api/chats/:id/messages?cursor=&limit=&q=` → `{ok, messages:[msg], next_cursor}` (по возрастанию id)
* `POST /api/chats/:id/messages` `{body, attachment?, attachment_kind?, reply_to?}` → `{ok, message}`
* `POST /api/chats/:id/read` `{message_id}` ; `POST /api/chats/:id/typing`
* `PATCH /api/messages/:id` `{body}` ; `DELETE /api/messages/:id` ; `POST /api/messages/:id/reactions` `{emoji}`

**chat:** `{id, kind:'dm'|'group'|'group'(community), title, avatar, description, community_id,
created_at, last_message_at, my_role, pinned, muted, last_read, unread, members_count,
peer?, peer_card?, last_message?}`

**msg:** `{id, chat_id, sender, sender_id, body, attachment, attachment_kind, reply_to, system,
mine, created_at, edited_at, deleted, reply?, reactions:[{emoji,count,mine}], reads}`

Realtime (уже приходит по WS): `message.new {chat_id, message, unread}`, `typing {chat_id,user}`,
`message.read {chat_id,message_id,user_id}`, `message.reaction`, `message.edited`,
`message.deleted {message_id}`, `presence {user_id,online,last_seen}`, `chat.new`, `chat.updated`.
Модуль обязан: подписаться на эти события, обновлять список чатов и открытый чат, слать
`OM.ws.typing(chatId)` при наборе (throttle 3 c), `OM.ws.read(chatId,lastId)` при просмотре,
и эмитить `OM.updateChatBadge(total)`.

### 3.5 Сообщества (`communities.js`)

* `GET /api/communities?q=&scope=all|mine` → `{ok, communities:[]}`
* `POST /api/communities` `{name, slug?, description?, kind:'group'|'channel', is_public?, avatar?}` → `{ok, community, chat_id}`
* `GET /api/communities/:id` (id | slug | `OMG-XXXX`) → `{ok, community, members:[card+role], invites:[]}`
* `PATCH|POST /api/communities/:id` `{name?,description?,avatar?,banner?,is_public?,kind?}`
* `DELETE /api/communities/:id`
* `POST /api/communities/:id/join` → `{ok, community}` ; `POST /api/communities/:id/leave`
* `POST /api/communities/:id/members/:uid/role` `{role:'member'|'admin'}` ; `DELETE /api/communities/:id/members/:uid`
* `POST /api/communities/:id/invites` `{expires_in?, max_uses?}` → `{ok, code, link}`
* `POST /api/communities/join/:code` → `{ok, community}`

**community:** `{id, orange_id, slug, name, description, avatar, banner, kind, is_public,
members, created_at, owner, is_member, my_role, chat_id, posts}`

### 3.6 Загрузка файлов

* `POST /api/upload?name=имя.jpg&kind=image` — тело: сам файл (`File`/`Blob`),
  `OM.api.upload(file, kind)` → `{url, kind, size}`; готовый URL вставлять в `media`/`avatar`/`attachment`.

## 4. Домашние DOM-узлы (уже есть в index.html)

```
#view-auth #auth-body
#view-feed: #feed-search #feed-search-btn #post-new-btn #feed-tabs #stories-bar
            #feed-composer #feed-trends #feed-list #feed-more
#view-chats: #chat-new-btn #chat-search #chat-list #chat-panel #chat-empty #chat-body
             #chat-head #chat-messages #chat-typing #chat-input #chat-send #chat-attach #chat-file
#view-communities: #community-search #community-search-btn #community-tabs #community-list
                   #community-new-btn #community-join-code
#view-community: #community-detail
#view-stories: #story-new-btn #stories-full
#view-profile: #profile-body
#view-settings: #settings-nav #settings-body
#story-viewer: #story-progress #story-stage #story-close #story-prev #story-next
#modal-root #toast-root #side-me #nav-badge-chats
```

## 5. Ответственность модулей

| Файл | Раздел | Ключевые функции |
|---|---|---|
| `auth.js` | `#view-auth` | 4 метода входа, регистрация, восстановление пароля |
| `feed.js` | `#view-feed` | лента с бесконечной прокруткой, композер, лайки, комментарии, поиск по хештегам/названиям, тренды |
| `stories.js` | `#stories-bar`, `#view-stories`, `#story-viewer` | создание, просмотр с прогресс-барами, отметка просмотра, зрители |
| `chats.js` | `#view-chats` | список чатов, сообщения, realtime, поиск, создание чатов/групп, вложения, реакции, набор текста, прочтение |
| `communities.js` | `#view-communities`, `#view-community` | список, создание, страница, посты, участники, роли, приглашения |
| `profile.js` | `#view-profile` | профиль, подписка, блокировка, посты пользователя, Orange ID |
| `settings.js` | `#view-settings` | профиль, приватность, безопасность (TOTP+QR, пароль, сессии, флеш-ключи), тема, админка |
| `qrcode.js` | — | `OM.qr.svg(text, size)` → SVG-строка с QR (версии 1–10, ECC M, byte mode) |
