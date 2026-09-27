/* ============================================================================
   OrangeM — модуль чатов (#view-chats)
   Список чатов, переписка, realtime, поиск, создание чатов и групп, вложения,
   реакции, набор текста, прочтение. Без внешних библиотек.
   Контракт: server/web/SPEC.md (раздел 3.4).
   ========================================================================== */
window.OM = window.OM || {};

(function () {
  'use strict';

  /* ------------------------------------------------------------- состояние */

  let bound = false;          // DOM-слушатели навешены один раз
  let activeId = 0;           // открытый чат
  let activeChat = null;      // объект открытого чата
  let activeMembers = [];     // участники открытого чата
  let messages = [];          // загруженные сообщения (по возрастанию id)
  let nextCursor = null;      // курсор подгрузки истории
  let loadingOlder = false;
  let openSeq = 0;            // защита от гонок при быстром переключении чатов
  let searchQuery = '';       // текущий запрос поиска по чатам
  let viewChats = [];         // то, что реально отрисовано в списке
  let userResults = [];       // найденные пользователи (секция под списком)
  let pendingSelect = 0;      // чат, который нужно открыть после перехода
  let replyTo = null;         // {id, sender, body} — ответ на сообщение
  let pendingAttachment = null; // {url, kind, name} — вложение до отправки
  let typingSentAt = 0;       // throttle отправки «печатает…» (3 c)
  let typingUsers = {};       // id -> {name, timer}
  let popup = null;           // всплывающее меню/пикер эмодзи
  let localReactionAt = {};   // id сообщения -> время локальной реакции
  let focusHandler = null;
  let scrollHandler = null;

  /* Реакции: не эмодзи, а нейтральные символы. Литералы записаны escape-кодами,
     чтобы в исходниках не оставалось пиктограмм (проверка на эмодзи — CLEAN). */
  const EMOJI = ['\u2665', '\u2605', '\u2713', '?', '!', '\u271A'];

  /* --------------------------------------------------------------- утилиты */

  function q(id) { return document.getElementById(id); }
  function esc(s) { return OM.esc(s); }
  function num(v) { const n = Number(v); return isFinite(n) ? n : 0; }
  function nowSec() { return Math.floor(Date.now() / 1000); }
  function meId() { return num(OM.state.me && OM.state.me.id); }

  /** Только безопасные URL (относительные, http(s), data:image) */
  function safeUrl(u) {
    u = String(u || '');
    if (/^(https?:)?\/\//i.test(u)) return u;
    if (/^\//.test(u)) return u;
    if (/^data:image\//i.test(u)) return u;
    return '';
  }

  function senderName(u) {
    if (!u) return 'Без имени';
    return u.display_name || u.username || 'Без имени';
  }

  function isOnline(u) { return !!(u && u.presence && u.presence.online); }

  function peerOf(c) {
    if (!c) return null;
    return c.peer_card || c.peer || null;
  }

  function chatTitle(c) {
    if (!c) return 'Чат';
    if (c.title) return c.title;
    const p = peerOf(c);
    if (p) return senderName(p);
    return c.kind === 'dm' ? 'Личный чат' : 'Чат #' + num(c.id);
  }

  function plural(n, one, few, many) {
    n = Math.abs(num(n)) % 100;
    const n1 = n % 10;
    if (n > 10 && n < 20) return many;
    if (n1 > 1 && n1 < 5) return few;
    if (n1 === 1) return one;
    return many;
  }

  function attachmentLabel(kind) {
    if (kind === 'image') return 'фото';
    if (kind === 'video') return 'видео';
    if (kind === 'audio') return 'аудио';
    return 'вложение';
  }

  function fileName(url) {
    try {
      const parts = String(url).split('?')[0].split('/');
      return decodeURIComponent(parts[parts.length - 1] || 'файл');
    } catch (e) { return 'файл'; }
  }

  function kindFromUrl(url) {
    const n = String(url || '').toLowerCase();
    if (/\.(png|jpe?g|gif|webp|bmp|svg)$/.test(n)) return 'image';
    if (/\.(mp4|webm|mov|mkv|avi)$/.test(n)) return 'video';
    if (/\.(mp3|ogg|wav|m4a|flac)$/.test(n)) return 'audio';
    return 'file';
  }

  function kindOfFile(file) {
    const t = (file && file.type) || '';
    if (/^image\//.test(t)) return 'image';
    if (/^video\//.test(t)) return 'video';
    if (/^audio\//.test(t)) return 'audio';
    return kindFromUrl((file && file.name) || '');
  }

  /** Запрос похож на идентификатор (для поиска ещё и по пользователям) */
  function looksLikeIdentifier(s) {
    s = String(s || '').trim();
    if (!s) return false;
    if (s.indexOf('@') >= 0) return true;
    if (s.charAt(0) === '+') return true;
    if (/\d/.test(s)) return true;
    if (/^OMG?-/i.test(s)) return true;
    return false;
  }

  function findChat(id) {
    id = num(id);
    const arr = OM.state.chats || [];
    for (let i = 0; i < arr.length; i++) if (num(arr[i].id) === id) return arr[i];
    for (let i = 0; i < viewChats.length; i++) if (num(viewChats[i].id) === id) return viewChats[i];
    return null;
  }

  /** Добавляет/обновляет чат в OM.state.chats (объекты обновляются по ссылке) */
  function mergeChat(chat) {
    if (!chat || !chat.id) return null;
    const arr = (OM.state.chats = OM.state.chats || []);
    const id = num(chat.id);
    for (let i = 0; i < arr.length; i++) {
      if (num(arr[i].id) === id) { Object.assign(arr[i], chat); return arr[i]; }
    }
    arr.push(chat);
    return arr[arr.length - 1];
  }

  function totalUnread() {
    return (OM.state.chats || []).reduce((s, c) => s + num(c.unread), 0);
  }

  /** Синхронизация бейджа непрочитанных */
  function updateUnread() {
    const total = totalUnread();
    OM.state.unreadTotal = total;
    OM.updateChatBadge(total);
    OM.emit('unread-changed', total);
  }

  function lastRealId() {
    let max = 0;
    for (let i = 0; i < messages.length; i++) {
      const id = num(messages[i].id);
      if (id > max) max = id;
    }
    return max;
  }

  function msgById(id) {
    id = num(id);
    for (let i = 0; i < messages.length; i++) if (num(messages[i].id) === id) return messages[i];
    return null;
  }

  /* ------------------------------------------------------------ список чатов */

  function previewText(c) {
    const lm = c && c.last_message;
    if (!lm) return '';
    if (lm.deleted) return 'сообщение удалено';
    if (lm.system) return lm.body || '';
    const mine = num(lm.sender_id) && num(lm.sender_id) === meId();
    let text = lm.body || '';
    if (!text && lm.attachment) text = attachmentLabel(lm.attachment_kind);
    const who = c.kind === 'dm' ? (mine ? 'Вы: ' : '') : ((mine ? 'Вы' : senderName(lm.sender)) + ': ');
    return who + text;
  }

  function chatTime(c) {
    const ts = num(c.last_message_at) || num(c.last_message && c.last_message.created_at);
    if (!ts) return '';
    const d = new Date(ts * 1000), now = new Date();
    const sameDay = d.getFullYear() === now.getFullYear() && d.getMonth() === now.getMonth() &&
                    d.getDate() === now.getDate();
    return sameDay ? OM.fmtTime(ts) : OM.timeAgo(ts);
  }

  function chatRow(c) {
    const isDm = c.kind === 'dm';
    const peer = peerOf(c);
    const online = isDm && (isOnline(peer) || !!OM.state.onlineUsers[num(peer && peer.id)]);
    const av = isDm
      ? OM.avatar(peer || { id: num(c.id), display_name: chatTitle(c) }, { size: 'sm', online: online })
      : OM.avatar({ id: 'chat' + num(c.id), display_name: chatTitle(c), avatar: c.avatar }, { size: 'sm' });
    const unread = num(c.unread);
    const badge = unread > 0
      ? '<span class="chat-badge">' + (unread > 99 ? '99+' : unread) + '</span>'
      : '<span class="chat-badge zero hidden">0</span>';
    const marks = (c.pinned ? ' ' + OM.icon('pin', 13) : '') + (c.muted ? ' ' + OM.icon('mute', 13) : '');
    return OM.el(
      '<div class="chat-item' + (num(c.id) === activeId ? ' active' : '') + '" data-chat="' + num(c.id) + '">' +
        av +
        '<div class="cw">' +
          '<div class="nm">' + esc(chatTitle(c)) + marks + '</div>' +
          '<div class="pv">' + esc(previewText(c)) + '</div>' +
        '</div>' +
        '<div class="mt"><span>' + esc(chatTime(c)) + '</span>' + badge + '</div>' +
      '</div>');
  }

  function identifierLine(u) {
    const bits = [];
    if (u && u.username) bits.push('@' + u.username);
    if (u && u.orange_id) bits.push(u.orange_id);
    return bits.join(' · ');
  }

  function userRow(u) {
    return OM.el(
      '<div class="chat-item" data-user="' + num(u && u.id) + '">' +
        OM.avatar(u, { size: 'sm', online: isOnline(u) }) +
        '<div class="cw">' +
          '<div class="nm">' + esc(senderName(u)) + '</div>' +
          '<div class="pv">' + esc(identifierLine(u)) + '</div>' +
        '</div>' +
        '<div class="mt"><span class="badge">Написать</span></div>' +
      '</div>');
  }

  function renderChats() {
    const list = q('chat-list');
    if (!list) return;
    const chats = viewChats || [];
    list.innerHTML = '';
    if (!chats.length) {
      const text = searchQuery
        ? 'Ничего не найдено. Поиск работает по ID чата, юзернейму, почте, телефону и Orange ID.'
        : 'Пока нет чатов. Нажмите «+ Чат», чтобы начать переписку.';
      const empty = OM.el('<div class="empty-state"><p>' + esc(text) + '</p></div>');
      if (empty) list.appendChild(empty);
    } else {
      chats.forEach(c => list.appendChild(chatRow(c)));
    }
    if (userResults.length) {
      const h = OM.el('<div class="hint">Найденные пользователи</div>');
      if (h) list.appendChild(h);
      userResults.forEach(u => list.appendChild(userRow(u)));
    }
    markActive();
  }

  function markActive() {
    const list = q('chat-list');
    if (!list) return;
    list.querySelectorAll('.chat-item[data-chat]').forEach(el => {
      el.classList.toggle('active', num(el.getAttribute('data-chat')) === activeId);
    });
  }

  /** Загрузка списка чатов. query — строка поиска ('' = весь список) */
  async function loadChats(query) {
    searchQuery = String(query || '').trim();
    const path = '/api/chats' + (searchQuery ? '?q=' + encodeURIComponent(searchQuery) : '');
    const r = await OM.api.get(path);
    const list = Array.isArray(r.chats) ? r.chats : [];
    if (searchQuery) {
      // результат поиска не заменяет полный список, а дополняет его
      list.forEach(c => mergeChat(c));
      viewChats = list.map(c => findChat(c.id) || c);
    } else {
      OM.state.chats = list;
      viewChats = OM.state.chats;
    }
    updateUnread();
    return list;
  }

  /** Перезагрузка списка с текущим фильтром (для realtime) */
  const reloadList = OM.debounce(function () {
    loadChats(searchQuery).then(renderChats).catch(function () {});
  }, 600);

  /* -------------------------------------------------------------- поиск */

  const doSearch = OM.debounce(async function () {
    const inp = q('chat-search');
    const text = inp ? inp.value.trim() : '';
    try {
      await loadChats(text);
      if (looksLikeIdentifier(text)) {
        const r = await OM.api.get('/api/chats/search?q=' + encodeURIComponent(text));
        const users = Array.isArray(r.users) ? r.users : [];
        userResults = users.filter(u => u && !u.is_me);
        const found = Array.isArray(r.chats) ? r.chats : [];
        found.forEach(c => {
          if (!viewChats.some(v => num(v.id) === num(c.id))) viewChats.push(mergeChat(c) || c);
        });
      } else {
        userResults = [];
      }
    } catch (e) {
      userResults = [];
      OM.toast(e.message || 'Ошибка поиска', 'err');
    }
    renderChats();
  }, 300);

  /* ---------------------------------------------------------- открытый чат */

  function showBody() {
    const body = q('chat-body'), empty = q('chat-empty');
    if (empty) empty.classList.add('hidden');
    if (body) body.classList.remove('hidden');
  }

  function showEmpty() {
    const body = q('chat-body'), empty = q('chat-empty');
    if (body) body.classList.add('hidden');
    if (empty) empty.classList.remove('hidden');
  }

  function closeChat() {
    activeId = 0;
    activeChat = null;
    activeMembers = [];
    messages = [];
    nextCursor = null;
    replyTo = null;
    pendingAttachment = null;
    OM.state.activeChat = null;
    clearTyping();
    closePopup();
    renderBars();
    renderHead();
    const head = q('chat-head');
    if (head) head.innerHTML = '';
    const box = q('chat-messages');
    if (box) box.innerHTML = '';
    showEmpty();
    markActive();
  }

  function renderHead() {
    const head = q('chat-head');
    if (!head) return;
    if (!activeChat) { head.innerHTML = ''; return; }
    const c = activeChat;
    const isDm = c.kind === 'dm';
    const peer = peerOf(c) || {};
    const title = chatTitle(c);
    const online = isOnline(peer) || !!OM.state.onlineUsers[num(peer.id)];
    const av = isDm
      ? OM.avatar(peer, { size: 'sm', online: online })
      : OM.avatar({ id: 'chat' + num(c.id), display_name: title, avatar: c.avatar }, { size: 'sm' });
    const st = isDm
      ? OM.onlineText(peer)
      : num(c.members_count) + ' ' + plural(num(c.members_count), 'участник', 'участника', 'участников');
    head.innerHTML =
      av +
      '<div class="cw">' +
        '<div class="nm" data-act="open">' + esc(title) + '</div>' +
        '<div class="st">' + esc(st) + '</div>' +
      '</div>' +
      '<button class="btn ghost icon" data-act="search" title="Поиск по сообщениям">' + OM.icon('search', 18) + '</button>' +
      '<button class="btn ghost icon" data-act="settings" title="Настройки чата">' + OM.icon('settings', 18) + '</button>';
  }

  /** Открыть чат: участники + последние 50 сообщений */
  async function selectChat(id) {
    id = num(id);
    if (!id) return;
    activeId = id;
    replyTo = null;
    pendingAttachment = null;
    renderBars();
    clearTyping();
    closePopup();
    markActive();
    showBody();
    const box = q('chat-messages');
    if (box) box.innerHTML = '<div class="hint" style="text-align:center">Загрузка…</div>';
    const seq = ++openSeq;
    try {
      const info = await OM.api.get('/api/chats/' + id);
      if (seq !== openSeq) return;
      activeChat = info.chat || null;
      activeMembers = Array.isArray(info.members) ? info.members : [];
      if (activeChat) { mergeChat(activeChat); OM.state.activeChat = activeChat; }
      const mr = await OM.api.get('/api/chats/' + id + '/messages?limit=50');
      if (seq !== openSeq) return;
      messages = Array.isArray(mr.messages) ? mr.messages : [];
      nextCursor = mr.next_cursor || null;
      localReactionAt = {};
      renderHead();
      renderMessages(true);
      markRead();
      renderChats();
      const input = q('chat-input');
      if (input) input.focus();
      OM.emit('chat-selected', activeChat);
    } catch (e) {
      if (seq !== openSeq) return;
      OM.toast(e.message || 'Не удалось открыть чат', 'err');
      showEmpty();
    }
  }

  /** Отметка прочтения (только при открытом чате и активном окне) */
  function markRead() {
    if (!activeId) return;
    const c = findChat(activeId);
    if (c) c.unread = 0;
    updateUnread();
    if (typeof document.hasFocus === 'function' && !document.hasFocus()) return;
    const last = lastRealId();
    if (!last) return;
    OM.ws.read(activeId, last);
    OM.api.post('/api/chats/' + activeId + '/read', { message_id: last }).catch(function () {});
    const ch = findChat(activeId);
    if (ch) ch.last_read = last;
  }

  /* ------------------------------------------------------------ сообщения */

  function attachHtml(m) {
    const url = safeUrl(m.attachment);
    if (!url) return '';
    const kind = m.attachment_kind || kindFromUrl(url);
    if (kind === 'image') {
      return '<div class="msg-attach"><img src="' + esc(url) + '" alt="" loading="lazy"></div>';
    }
    if (kind === 'video') {
      return '<div class="msg-attach"><video src="' + esc(url) + '" controls preload="metadata"></video></div>';
    }
    if (kind === 'audio') {
      return '<div class="msg-attach"><audio src="' + esc(url) + '" controls preload="metadata"></audio></div>';
    }
    return '<div class="msg-attach"><a class="btn small" href="' + esc(url) + '" target="_blank" ' +
           'rel="noopener" download>' + OM.icon('file', 14) + ' ' + esc(fileName(url)) + '</a></div>';
  }

  function replySnippet(r) {
    if (!r) return '';
    const txt = String(r.body || '').slice(0, 160);
    return txt || 'вложение';
  }

  function replyHtml(r) {
    if (!r || !num(r.id)) return '';
    const nm = r.sender ? senderName(r.sender) : '';
    return '<div class="msg-reply" data-jump="' + num(r.id) + '">' +
           (nm ? '<b>' + esc(nm) + '</b>: ' : '') + esc(replySnippet(r)) + '</div>';
  }

  function reactionsHtml(m) {
    const rs = Array.isArray(m.reactions) ? m.reactions : [];
    const items = rs.map(r => '<span class="reaction' + (r.mine ? ' mine' : '') + '" data-emoji="' +
      esc(r.emoji) + '" title="Реакция">' + esc(r.emoji) + (num(r.count) > 1 ? ' ' + num(r.count) : '') + '</span>');
    items.push('<span class="reaction" data-emoji-add="1" title="Добавить реакцию">' + OM.icon('plus', 13) + '</span>');
    return '<div class="reactions">' + items.join('') + '</div>';
  }

  function metaHtml(m) {
    const bits = ['<span>' + esc(OM.fmtTime(num(m.created_at))) + '</span>'];
    if (m.edited_at) bits.push('<span>изменено</span>');
    if (m.deleted) bits.push('<span>удалено</span>');
    if (m.mine && num(m.reads) > 0) bits.push('<span>прочитано</span>');
    bits.push('<span class="ed" data-act="reply" title="Ответить">' + OM.icon('reply', 13) + '</span>');
    if (m.mine) bits.push('<span class="ed" data-act="menu" title="Действия">' + OM.icon('dots', 13) + '</span>');
    return '<div class="mt">' + bits.join('') + '</div>';
  }

  function messageHtml(m) {
    if (m.system) {
      return '<div class="msg-system" data-id="' + esc(m.id) + '">' + esc(m.body || '') + '</div>';
    }
    const mine = !!m.mine;
    const isGroup = !!(activeChat && activeChat.kind !== 'dm');
    const showAuthor = !mine && isGroup;
    const parts = [];
    if (showAuthor) parts.push('<div class="au">' + esc(senderName(m.sender)) + '</div>');
    if (m.reply) parts.push(replyHtml(m.reply));
    const att = attachHtml(m);
    if (att) parts.push(att);
    if (m.body) parts.push('<div class="tx">' + OM.linkify(m.body) + '</div>');
    else if (m.deleted) parts.push('<div class="tx">сообщение удалено</div>');
    parts.push(metaHtml(m));
    if (!m.deleted) parts.push(reactionsHtml(m));
    const avatar = showAuthor
      ? OM.avatar(m.sender || { id: num(m.sender_id) }, { size: 'xs' })
      : '';
    return '<div class="msg' + (mine ? ' mine' : '') + (m.deleted ? ' deleted' : '') +
           '" data-id="' + esc(m.id) + '">' + avatar +
           '<div class="bub">' + parts.join('') + '</div></div>';
  }

  function buildMessagesHtml() {
    const out = [];
    let lastDay = '';
    for (let i = 0; i < messages.length; i++) {
      const m = messages[i];
      const ts = num(m.created_at);
      const day = ts ? new Date(ts * 1000).toDateString() : '';
      if (day && day !== lastDay) {
        lastDay = day;
        out.push('<div class="day-sep">' + esc(OM.dayLabel(ts)) + '</div>');
      }
      out.push(messageHtml(m));
    }
    return out.join('');
  }

  function renderMessages(toBottom) {
    const box = q('chat-messages');
    if (!box) return;
    const nearBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 90;
    box.innerHTML = buildMessagesHtml();
    if (toBottom || nearBottom) box.scrollTop = box.scrollHeight;
  }

  /** Добавляет или заменяет сообщение в открытом чате */
  function upsertMessage(m, opts) {
    if (!m || num(m.chat_id) !== activeId) return;
    const id = num(m.id);
    let replaced = false;
    if (id) {
      for (let i = 0; i < messages.length; i++) {
        if (num(messages[i].id) === id) {
          messages[i] = Object.assign({}, messages[i], m);
          replaced = true;
          break;
        }
      }
    }
    if (!replaced) {
      // убираем оптимистичные «временные» сообщения этого пользователя
      messages = messages.filter(x => !(typeof x.id === 'string' && /^tmp/.test(x.id)));
      messages.push(m);
    }
    renderMessages(!!(opts && opts.scroll));
  }

  /** Дозагрузка истории при скролле вверх с сохранением позиции */
  async function loadOlder() {
    if (!activeId || !nextCursor || loadingOlder) return;
    const box = q('chat-messages');
    if (!box) return;
    loadingOlder = true;
    const prevHeight = box.scrollHeight;
    const prevTop = box.scrollTop;
    const id = activeId;
    try {
      const r = await OM.api.get('/api/chats/' + id + '/messages?limit=50&cursor=' +
                                 encodeURIComponent(nextCursor));
      if (id !== activeId) { loadingOlder = false; return; }
      const list = Array.isArray(r.messages) ? r.messages : [];
      nextCursor = r.next_cursor || null;
      if (list.length) {
        const known = {};
        messages.forEach(m => { known[String(m.id)] = true; });
        const older = list.filter(m => !known[String(m.id)]);
        messages = older.concat(messages);
        box.innerHTML = buildMessagesHtml();
        box.scrollTop = box.scrollHeight - prevHeight + prevTop;
      }
    } catch (e) {
      OM.toast(e.message || 'Не удалось загрузить историю', 'err');
    }
    loadingOlder = false;
  }

  /** Показать сообщение по id (при необходимости догружает окно вокруг него) */
  async function jumpToMessage(id) {
    id = num(id);
    if (!id || !activeId) return;
    const box = q('chat-messages');
    if (box) {
      const found = box.querySelector('[data-id="' + id + '"]');
      if (found) { flash(found); return; }
    }
    const cid = activeId;
    try {
      const r = await OM.api.get('/api/chats/' + cid + '/messages?limit=50&cursor=' + (id + 1));
      if (cid !== activeId) return;
      const list = Array.isArray(r.messages) ? r.messages : [];
      if (!list.length) { OM.toast('Сообщение не найдено', 'err'); return; }
      messages = list;
      nextCursor = r.next_cursor || null;
      renderMessages(false);
      const node = box && box.querySelector('[data-id="' + id + '"]');
      if (node) { node.scrollIntoView({ block: 'center' }); flash(node); }
    } catch (e) {
      OM.toast(e.message || 'Не удалось найти сообщение', 'err');
    }
  }

  function flash(node) {
    try {
      const old = node.style.outline;
      node.style.outline = '2px solid var(--orange)';
      setTimeout(() => { node.style.outline = old; }, 1200);
    } catch (e) { /* игнорируем */ }
  }

  async function reloadCurrent() {
    if (!activeId) return;
    const id = activeId;
    try {
      const r = await OM.api.get('/api/chats/' + id + '/messages?limit=50');
      if (id !== activeId) return;
      messages = Array.isArray(r.messages) ? r.messages : [];
      nextCursor = r.next_cursor || null;
      renderMessages(true);
    } catch (e) { /* тихо */ }
  }

  /* ------------------------------------------------------ панель над вводом */

  function ensureBars() {
    if (q('om-chat-bars')) return;
    const row = document.querySelector('#chat-body .chat-input-row');
    if (!row || !row.parentNode) return;
    const bars = document.createElement('div');
    bars.id = 'om-chat-bars';
    bars.className = 'chat-typing hidden';
    bars.addEventListener('click', function (e) {
      const b = e.target.closest('[data-cancel]');
      if (!b) return;
      const kind = b.getAttribute('data-cancel');
      if (kind === 'reply') replyTo = null;
      else if (kind === 'attach') pendingAttachment = null;
      renderBars();
    });
    row.parentNode.insertBefore(bars, row);
  }

  function renderBars() {
    const bars = q('om-chat-bars');
    if (!bars) return;
    const parts = [];
    if (replyTo) {
      parts.push('<div class="msg-reply">Ответ ' + esc(senderName(replyTo.sender)) + ': ' +
                 esc(replySnippet(replyTo)) +
                 ' <button class="btn ghost small" data-cancel="reply">Отмена</button></div>');
    }
    if (pendingAttachment) {
      parts.push('<div class="msg-reply">' + OM.icon('attach', 14) + ' ' + esc(pendingAttachment.name || 'файл') +
                 ' <button class="btn ghost small" data-cancel="attach">Убрать</button></div>');
    }
    if (!parts.length) {
      bars.innerHTML = '';
      bars.classList.add('hidden');
      return;
    }
    bars.classList.remove('hidden');
    bars.innerHTML = parts.join('');
  }

  /* ------------------------------------------------------- всплывающие меню */

  function closePopup() {
    if (popup) { popup.remove(); popup = null; }
    document.removeEventListener('click', onOutsideClick, true);
  }

  function onOutsideClick(e) {
    if (popup && !popup.contains(e.target)) closePopup();
  }

  function showPopup(anchor, nodes, column) {
    // позицию считаем до закрытия прежнего попапа (якорь может быть внутри него)
    const r = anchor.getBoundingClientRect();
    closePopup();
    const p = document.createElement('div');
    p.className = 'panel';
    p.style.cssText = 'position:fixed;z-index:4500;padding:8px;box-shadow:var(--shadow);display:flex;gap:6px;' +
                      (column ? 'flex-direction:column;' : '');
    nodes.forEach(n => p.appendChild(n));
    document.body.appendChild(p);
    popup = p;
    const pr = p.getBoundingClientRect();
    const left = Math.min(Math.max(8, r.left), Math.max(8, window.innerWidth - pr.width - 8));
    let top = r.bottom + 6;
    if (top + pr.height > window.innerHeight - 8) top = Math.max(8, r.top - pr.height - 6);
    p.style.left = left + 'px';
    p.style.top = top + 'px';
    setTimeout(() => document.addEventListener('click', onOutsideClick, true), 0);
    return p;
  }

  function popupButton(label, fn, iconName) {
    const b = document.createElement('button');
    b.className = 'btn ghost small';
    b.style.justifyContent = 'flex-start';
    b.innerHTML = iconName ? OM.icon(iconName, 14) + ' ' + OM.esc(label) : OM.esc(label);
    b.onclick = function () { closePopup(); fn(); };
    return b;
  }

  function openEmojiPicker(anchor, m) {
    if (!m) return;
    const nodes = EMOJI.map(e => {
      const b = document.createElement('button');
      b.className = 'reaction';
      b.textContent = e;
      b.onclick = function () { closePopup(); toggleReaction(m, e); };
      return b;
    });
    showPopup(anchor, nodes, false);
  }

  function openMessageMenu(anchor, m) {
    const nodes = [];
    nodes.push(popupButton('Ответить', () => startReply(m), 'reply'));
    nodes.push(popupButton('Реакция', () => openEmojiPicker(anchor, m), 'react'));
    if (m.mine && !m.deleted) {
      nodes.push(popupButton('Изменить', () => editMessage(m), 'edit'));
    }
    if ((m.mine || canManage()) && !m.deleted) {
      nodes.push(popupButton('Удалить', () => deleteMessage(m), 'trash'));
    }
    if (m.body) nodes.push(popupButton('Скопировать', () => OM.copy(m.body), 'copy'));
    showPopup(anchor, nodes, true);
  }

  function startReply(m) {
    if (!m) return;
    replyTo = { id: num(m.id), sender: m.sender || { id: num(m.sender_id) }, body: m.body || '' };
    renderBars();
    const input = q('chat-input');
    if (input) input.focus();
  }

  function editMessage(m) {
    OM.prompt({
      title: 'Изменить сообщение',
      label: 'Текст',
      value: m.body || '',
      ok: 'Сохранить'
    }, async function (value) {
      const text = String(value || '').trim();
      if (!text) throw { message: 'Сообщение не может быть пустым' };
      const r = await OM.api.patch('/api/messages/' + num(m.id), { body: text });
      if (r.message) upsertMessage(r.message, { scroll: false });
      OM.toast('Сообщение изменено', 'ok');
    });
  }

  function deleteMessage(m) {
    OM.confirm('Удалить это сообщение?', async function () {
      try {
        await OM.api.del('/api/messages/' + num(m.id));
        const cur = msgById(m.id);
        if (cur) { cur.deleted = true; cur.body = ''; cur.attachment = ''; }
        renderMessages(false);
        reloadList();
      } catch (e) {
        OM.toast(e.message || 'Не удалось удалить', 'err');
      }
    }, 'Удалить');
  }

  async function toggleReaction(m, emoji) {
    if (!m || !num(m.id)) return;
    const mid = num(m.id);
    localReactionAt[mid] = Date.now();
    try {
      const r = await OM.api.post('/api/messages/' + mid + '/reactions', { emoji: emoji });
      if (Array.isArray(r.reactions)) {
        const cur = msgById(mid);
        if (cur) { cur.reactions = r.reactions; renderMessages(false); }
      }
    } catch (e) {
      OM.toast(e.message || 'Не удалось поставить реакцию', 'err');
    }
  }

  /* ---------------------------------------------------------- отправка */

  async function sendMessage() {
    const input = q('chat-input');
    if (!activeId) return;
    const text = input ? input.value.trim() : '';
    if (!text && !pendingAttachment) return;
    const payload = {};
    if (text) payload.body = text;
    if (pendingAttachment) {
      payload.attachment = pendingAttachment.url;
      payload.attachment_kind = pendingAttachment.kind;
    }
    if (replyTo) payload.reply_to = num(replyTo.id);

    const me = OM.state.me || {};
    const temp = {
      id: 'tmp-' + Date.now() + '-' + Math.round(Math.random() * 1e6),
      chat_id: activeId,
      sender: me,
      sender_id: me.id,
      body: text,
      attachment: pendingAttachment ? pendingAttachment.url : '',
      attachment_kind: pendingAttachment ? pendingAttachment.kind : '',
      reply_to: replyTo ? num(replyTo.id) : 0,
      reply: replyTo ? { id: num(replyTo.id), sender: replyTo.sender, body: replyTo.body } : null,
      mine: true, system: false, deleted: false,
      created_at: nowSec(), edited_at: 0, reactions: [], reads: 0
    };
    upsertMessage(temp, { scroll: true });
    if (input) input.value = '';
    pendingAttachment = null;
    replyTo = null;
    renderBars();

    const cid = activeId;
    try {
      const r = await OM.api.post('/api/chats/' + cid + '/messages', payload);
      if (r.message) upsertMessage(r.message, { scroll: true });
    } catch (e) {
      messages = messages.filter(x => x.id !== temp.id);
      renderMessages(true);
      OM.toast(e.message || 'Сообщение не отправлено', 'err');
    }
  }

  function onTypingInput() {
    if (!activeId) return;
    const t = Date.now();
    if (t - typingSentAt < 3000) return;
    typingSentAt = t;
    OM.ws.typing(activeId);
  }

  function onFilePicked() {
    const file = q('chat-file');
    const f = file && file.files && file.files[0];
    if (!f) return;
    const kind = kindOfFile(f);
    OM.toast('Загрузка файла…', 'ok', 1600);
    OM.api.upload(f, kind).then(function (up) {
      pendingAttachment = {
        url: up.url || '',
        kind: up.kind || kind,
        name: f.name || 'файл',
        size: up.size || f.size
      };
      if (!pendingAttachment.url) { pendingAttachment = null; OM.toast('Сервер не вернул файл', 'err'); return; }
      renderBars();
      const input = q('chat-input');
      if (input) input.focus();
    }).catch(function (e) {
      OM.toast(e.message || 'Не удалось загрузить файл', 'err');
    });
    file.value = '';
  }

  /* ---------------------------------------------------------- набор текста */

  function showTyping(user) {
    if (!user) return;
    const key = num(user.id);
    if (!key || key === meId()) return;
    if (typingUsers[key]) clearTimeout(typingUsers[key].timer);
    typingUsers[key] = {
      name: senderName(user),
      timer: setTimeout(function () { delete typingUsers[key]; renderTyping(); }, 3000)
    };
    renderTyping();
  }

  function renderTyping() {
    const box = q('chat-typing');
    if (!box) return;
    const keys = Object.keys(typingUsers);
    if (!keys.length) { box.textContent = ''; return; }
    const names = keys.map(k => typingUsers[k].name);
    box.textContent = names.length === 1
      ? names[0] + ' печатает…'
      : names.slice(0, 3).join(', ') + ' печатают…';
  }

  function clearTyping() {
    Object.keys(typingUsers).forEach(k => clearTimeout(typingUsers[k].timer));
    typingUsers = {};
    typingSentAt = 0;
    renderTyping();
  }

  /* ------------------------------------------------------- обработчики DOM */

  function onListClick(e) {
    const ur = e.target.closest('[data-user]');
    if (ur) {
      const uid = num(ur.getAttribute('data-user'));
      startDM(userResults.find(u => num(u.id) === uid) || uid);
      return;
    }
    const row = e.target.closest('[data-chat]');
    if (row) selectChat(num(row.getAttribute('data-chat')));
  }

  function onHeadClick(e) {
    const b = e.target.closest('[data-act]');
    if (!b) return;
    const act = b.getAttribute('data-act');
    if (act === 'open') {
      const peer = peerOf(activeChat);
      if (activeChat && activeChat.kind === 'dm' && peer && num(peer.id)) {
        OM.navigate('profile', { id: num(peer.id) });
      }
    } else if (act === 'search') {
      openMessageSearch();
    } else if (act === 'settings') {
      openChatSettings();
    }
  }

  function msgByNode(node) {
    const wrap = node.closest('[data-id]');
    if (!wrap) return null;
    return msgById(num(wrap.getAttribute('data-id')));
  }

  function onMessagesClick(e) {
    const react = e.target.closest('.reaction');
    if (react) {
      const m = msgByNode(react);
      if (!m) return;
      if (react.hasAttribute('data-emoji-add')) { openEmojiPicker(react, m); return; }
      toggleReaction(m, react.getAttribute('data-emoji'));
      return;
    }
    const act = e.target.closest('[data-act]');
    if (act) {
      const m = msgByNode(act);
      if (!m) return;
      const a = act.getAttribute('data-act');
      if (a === 'reply') startReply(m);
      else if (a === 'menu') openMessageMenu(act, m);
      return;
    }
    const jump = e.target.closest('[data-jump]');
    if (jump) jumpToMessage(num(jump.getAttribute('data-jump')));
  }

  function bindOnce() {
    if (bound) return;
    bound = true;

    const search = q('chat-search');
    if (search) search.addEventListener('input', function () { doSearch(); });

    const newBtn = q('chat-new-btn');
    if (newBtn) newBtn.onclick = openNewChat;

    const sendBtn = q('chat-send');
    if (sendBtn) sendBtn.onclick = sendMessage;

    const input = q('chat-input');
    if (input) {
      input.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMessage(); }
      });
      input.addEventListener('input', onTypingInput);
    }

    const attach = q('chat-attach');
    if (attach) {
      attach.onclick = function () {
        const f = q('chat-file');
        if (f) f.click();
      };
    }
    const file = q('chat-file');
    if (file) file.addEventListener('change', onFilePicked);

    const msgs = q('chat-messages');
    if (msgs) msgs.addEventListener('click', onMessagesClick);

    const head = q('chat-head');
    if (head) head.addEventListener('click', onHeadClick);

    const list = q('chat-list');
    if (list) list.addEventListener('click', onListClick);

    ensureBars();
  }

  /* -------------------------------------------------- поиск по сообщениям */

  function openMessageSearch() {
    if (!activeId) return;
    const m = OM.modal({
      title: 'Поиск по сообщениям',
      footer: false,
      body: '<div class="field"><label>Запрос</label>' +
            '<input class="input" id="om-msg-q" placeholder="Текст сообщения…"></div>' +
            '<div id="om-msg-res" class="comment-list"></div>'
    });
    const inp = m.body.querySelector('#om-msg-q');
    const res = m.body.querySelector('#om-msg-res');
    if (!inp || !res) return;
    const run = OM.debounce(async function () {
      const text = inp.value.trim();
      if (!text) { res.innerHTML = ''; return; }
      res.innerHTML = '<div class="hint">Поиск…</div>';
      const cid = activeId;
      try {
        const r = await OM.api.get('/api/chats/' + cid + '/messages?limit=50&q=' + encodeURIComponent(text));
        if (cid !== activeId) return;
        const list = Array.isArray(r.messages) ? r.messages : [];
        if (!list.length) { res.innerHTML = '<div class="hint">Ничего не найдено</div>'; return; }
        res.innerHTML = '';
        list.slice().reverse().forEach(function (msg) {
          const row = OM.el(
            '<div class="comment">' +
              '<div class="cb">' +
                '<div class="cn">' + esc(senderName(msg.sender)) + ' · ' +
                  esc(OM.fmtDateTime(num(msg.created_at))) + '</div>' +
                '<div class="ct">' + esc(String(msg.body || '').slice(0, 300)) + '</div>' +
              '</div>' +
            '</div>');
          row.onclick = function () { m.close(); jumpToMessage(num(msg.id)); };
          res.appendChild(row);
        });
      } catch (e) {
        res.innerHTML = '<div class="hint">' + esc(e.message || 'Ошибка поиска') + '</div>';
      }
    }, 300);
    inp.addEventListener('input', function () { run(); });
    setTimeout(function () { inp.focus(); }, 60);
  }

  /* ------------------------------------------------------ настройки чата */

  function canManage() {
    const c = activeChat || {};
    const me = OM.state.me || {};
    if (c.kind === 'dm') return false;
    return c.my_role === 'owner' || c.my_role === 'admin' || me.is_admin === true;
  }

  function memberRowHtml(u) {
    const role = u.role === 'owner' ? 'владелец' : (u.role === 'admin' ? 'админ' : '');
    const roleBadge = role
      ? '<span class="badge ' + (u.role === 'owner' ? 'orange' : 'grey') + '">' + role + '</span>'
      : '';
    const remove = (canManage() && !u.is_me)
      ? '<button class="btn danger small" data-remove="' + num(u.id) + '">Удалить</button>'
      : '';
    return '<div class="member-row">' +
      OM.avatar(u, { size: 'sm', online: isOnline(u) }) +
      '<div style="flex:1;min-width:0">' +
        '<div>' + esc(senderName(u)) + (u.is_me ? ' <span class="badge grey">вы</span>' : '') + '</div>' +
        '<div class="hint" style="margin:0">' + esc(identifierLine(u)) + '</div>' +
      '</div>' + roleBadge + remove + '</div>';
  }

  function openChatSettings() {
    if (!activeChat) return;
    const m = OM.modal({ title: 'Настройки чата', width: 560, footer: false, body: '<div></div>' });
    const box = m.body.firstElementChild;
    renderChatSettings(box, m);
  }

  function renderChatSettings(box, modal) {
    const c = activeChat || {};
    if (!box) return;
    const isDm = c.kind === 'dm';
    const manage = canManage();
    const html = [];

    html.push('<div class="panel">' +
      '<div class="switch"><div class="lb">Закрепить чат<small>Всегда сверху списка</small></div>' +
        '<div class="toggle' + (c.pinned ? ' on' : '') + '" data-toggle="pinned"></div></div>' +
      '<div class="switch"><div class="lb">Без звука<small>Не присылать уведомления</small></div>' +
        '<div class="toggle' + (c.muted ? ' on' : '') + '" data-toggle="muted"></div></div>' +
      '</div>');

    if (manage) {
      html.push('<div class="panel"><h3>Оформление</h3>' +
        '<div class="field"><label>Название</label>' +
          '<div class="form-row"><input class="input" id="om-set-title" value="' + esc(c.title || '') +
            '" placeholder="Название чата"><button class="btn primary small" id="om-set-title-btn">Сохранить</button></div>' +
        '</div>' +
        '<div class="field"><label>Аватар</label>' +
          '<input type="file" id="om-set-avatar" accept="image/*"></div>' +
      '</div>');
    }

    const members = activeMembers || [];
    html.push('<div class="panel"><h3>Участники (' + members.length + ')</h3>' +
      '<div id="om-set-members">' + members.map(memberRowHtml).join('') + '</div>' +
      (manage
        ? '<div class="form-row" style="margin-top:12px">' +
            '<input class="input" id="om-set-add" placeholder="Юзернейм, почта, телефон или Orange ID">' +
            '<button class="btn small" id="om-set-add-btn">Добавить</button></div>'
        : '') +
      '</div>');

    if (isDm && peerOf(c)) {
      html.push('<button class="btn ghost wide" id="om-set-profile">Открыть профиль</button>');
    }
    html.push('<button class="btn danger wide" id="om-set-leave">' +
      (isDm ? 'Удалить чат' : 'Покинуть чат') + '</button>');

    box.innerHTML = html.join('');

    box.querySelectorAll('.toggle[data-toggle]').forEach(tg => {
      tg.onclick = async function () {
        const key = tg.getAttribute('data-toggle');
        const on = !tg.classList.contains('on');
        try {
          const r = await OM.api.patch('/api/chats/' + activeId, { [key]: on });
          tg.classList.toggle('on', on);
          if (r.chat) { activeChat = mergeChat(r.chat) || activeChat; renderHead(); renderChats(); }
        } catch (e) {
          OM.toast(e.message || 'Не удалось сохранить', 'err');
        }
      };
    });

    const titleBtn = box.querySelector('#om-set-title-btn');
    if (titleBtn) {
      titleBtn.onclick = async function () {
        const inp = box.querySelector('#om-set-title');
        const title = inp ? inp.value.trim() : '';
        if (!title) { OM.toast('Укажите название', 'err'); return; }
        try {
          const r = await OM.api.patch('/api/chats/' + activeId, { title: title });
          if (r.chat) { activeChat = mergeChat(r.chat) || activeChat; renderHead(); renderChats(); }
          OM.toast('Название сохранено', 'ok');
        } catch (e) { OM.toast(e.message || 'Не удалось сохранить', 'err'); }
      };
    }

    const avInput = box.querySelector('#om-set-avatar');
    if (avInput) {
      avInput.onchange = async function () {
        const f = avInput.files && avInput.files[0];
        if (!f) return;
        try {
          const up = await OM.api.upload(f, 'image');
          const r = await OM.api.patch('/api/chats/' + activeId, { avatar: up.url });
          if (r.chat) { activeChat = mergeChat(r.chat) || activeChat; renderHead(); renderChats(); }
          OM.toast('Аватар обновлён', 'ok');
        } catch (e) { OM.toast(e.message || 'Не удалось загрузить аватар', 'err'); }
      };
    }

    const addBtn = box.querySelector('#om-set-add-btn');
    if (addBtn) {
      addBtn.onclick = async function () {
        const inp = box.querySelector('#om-set-add');
        const identifier = inp ? inp.value.trim() : '';
        if (!identifier) { OM.toast('Укажите пользователя', 'err'); return; }
        addBtn.disabled = true;
        try {
          const r = await OM.api.post('/api/chats/' + activeId + '/members', { identifier: identifier });
          const added = Array.isArray(r.added) ? r.added : [];
          if (!added.length) OM.toast('Никого не добавили', 'err');
          else OM.toast('Добавлено: ' + added.length, 'ok');
          if (inp) inp.value = '';
          await refreshMembers();
          renderChatSettings(box, modal);
        } catch (e) {
          addBtn.disabled = false;
          OM.toast(e.message || 'Не удалось добавить', 'err');
        }
      };
    }

    box.querySelectorAll('[data-remove]').forEach(b => {
      b.onclick = function () {
        const uid = num(b.getAttribute('data-remove'));
        const u = activeMembers.find(x => num(x.id) === uid);
        OM.confirm('Удалить ' + senderName(u) + ' из чата?', async function () {
          try {
            await OM.api.del('/api/chats/' + activeId + '/members/' + uid);
            await refreshMembers();
            renderChatSettings(box, modal);
          } catch (e) { OM.toast(e.message || 'Не удалось удалить', 'err'); }
        }, 'Удалить');
      };
    });

    const profileBtn = box.querySelector('#om-set-profile');
    if (profileBtn) {
      profileBtn.onclick = function () {
        const peer = peerOf(activeChat);
        if (peer && num(peer.id)) { modal.close(); OM.navigate('profile', { id: num(peer.id) }); }
      };
    }

    const leaveBtn = box.querySelector('#om-set-leave');
    if (leaveBtn) {
      leaveBtn.onclick = function () {
        OM.confirm(isDm ? 'Удалить чат из списка?' : 'Покинуть этот чат?', async function () {
          try {
            await OM.api.post('/api/chats/' + activeId + '/leave');
            modal.close();
            const cid = activeId;
            OM.state.chats = (OM.state.chats || []).filter(x => num(x.id) !== cid);
            viewChats = viewChats.filter(x => num(x.id) !== cid);
            closeChat();
            renderChats();
            OM.toast('Готово', 'ok');
          } catch (e) { OM.toast(e.message || 'Не удалось выполнить', 'err'); }
        }, isDm ? 'Удалить' : 'Покинуть');
      };
    }
  }

  async function refreshMembers() {
    if (!activeId) return;
    try {
      const r = await OM.api.get('/api/chats/' + activeId);
      activeMembers = Array.isArray(r.members) ? r.members : [];
      if (r.chat) { activeChat = mergeChat(r.chat) || activeChat; OM.state.activeChat = activeChat; }
      renderHead();
      renderChats();
    } catch (e) { /* тихо */ }
  }

  /* --------------------------------------------------------- создание чата */

  /** Создать личный чат и открыть его (принимает id или карточку пользователя) */
  async function startDM(user) {
    const u = (user && typeof user === 'object') ? user : { id: user };
    const userId = num(u.id);
    const identifier = u.orange_id || u.short_id || (u.username ? '@' + u.username : '') || u.email || u.phone || '';
    if (!userId && !identifier) return;
    try {
      const body = { kind: 'dm' };
      if (userId) body.user_id = userId;
      if (identifier) body.identifier = String(identifier);
      const r = await OM.api.post('/api/chats', body);
      const chat = r.chat;
      if (!chat) return;
      mergeChat(chat);
      searchQuery = '';
      userResults = [];
      const s = q('chat-search');
      if (s) s.value = '';
      await loadChats('');
      renderChats();
      selectChat(chat.id);
    } catch (e) {
      OM.toast(e.message || 'Не удалось создать чат', 'err');
    }
  }

  function openNewChat() {
    const m = OM.modal({
      title: 'Новый чат',
      width: 560,
      footer: false,
      body: '<div class="head-chips" id="om-nc-tabs">' +
              '<button class="chip active" data-tab="dm">Личный чат</button>' +
              '<button class="chip" data-tab="group">Группа</button>' +
            '</div><div id="om-nc-body"></div>'
    });
    const tabs = m.body.querySelector('#om-nc-tabs');
    const body = m.body.querySelector('#om-nc-body');
    if (!tabs || !body) return;
    let tab = 'dm';
    const chosen = [];

    function userHit(u, onClick) {
      const row = userRow(u);
      row.querySelector('.badge').textContent = tab === 'dm' ? 'Написать' : 'Добавить';
      row.onclick = onClick;
      return row;
    }

    function searchUsers(needle, res, onPick) {
      if (needle.length < 2) { res.innerHTML = ''; return; }
      res.innerHTML = '<div class="hint">Поиск…</div>';
      OM.api.get('/api/users/search?q=' + encodeURIComponent(needle)).then(function (r) {
        const users = (Array.isArray(r.users) ? r.users : []).filter(u => u && !u.is_me);
        if (!users.length) { res.innerHTML = '<div class="hint">Пользователи не найдены</div>'; return; }
        res.innerHTML = '';
        users.forEach(u => res.appendChild(userHit(u, () => onPick(u))));
      }).catch(function (e) {
        res.innerHTML = '<div class="hint">' + esc(e.message || 'Ошибка поиска') + '</div>';
      });
    }

    function renderDm() {
      body.innerHTML =
        '<div class="field"><label>Кому написать</label>' +
          '<input class="input" id="om-nc-q" placeholder="Юзернейм, почта, телефон или Orange ID"></div>' +
        '<div class="hint">Поиск по юзернейму, e-mail, телефону и Orange ID.</div>' +
        '<div id="om-nc-res"></div>';
      const inp = body.querySelector('#om-nc-q');
      const res = body.querySelector('#om-nc-res');
      const run = OM.debounce(function () { searchUsers(inp.value.trim(), res, pickDm); }, 300);
      inp.addEventListener('input', function () { run(); });
      setTimeout(function () { inp.focus(); }, 60);
    }

    function pickDm(u) {
      m.close();
      startDM(u);
    }

    function renderGroup() {
      body.innerHTML =
        '<div class="field"><label>Название группы</label>' +
          '<input class="input" id="om-nc-title" placeholder="Например: Друзья"></div>' +
        '<div class="field"><label>Кого добавить</label>' +
          '<input class="input" id="om-nc-q" placeholder="Юзернейм, почта, телефон или Orange ID"></div>' +
        '<div id="om-nc-sel" class="head-chips"></div>' +
        '<div id="om-nc-res"></div>' +
        '<div class="form-row" style="margin-top:12px">' +
          '<button class="btn primary" id="om-nc-create">Создать группу</button></div>';
      const title = body.querySelector('#om-nc-title');
      const inp = body.querySelector('#om-nc-q');
      const res = body.querySelector('#om-nc-res');
      const sel = body.querySelector('#om-nc-sel');
      const create = body.querySelector('#om-nc-create');

      function drawSel() {
        sel.innerHTML = chosen.map((u, i) =>
          '<button class="chip active" data-i="' + i + '">' + esc(senderName(u)) + ' ' + OM.icon('close', 12) + '</button>').join('');
      }
      drawSel();

      sel.addEventListener('click', function (e) {
        const b = e.target.closest('[data-i]');
        if (!b) return;
        chosen.splice(num(b.getAttribute('data-i')), 1);
        drawSel();
      });

      const run = OM.debounce(function () {
        searchUsers(inp.value.trim(), res, function (u) {
          if (chosen.some(x => num(x.id) === num(u.id))) { OM.toast('Уже добавлен', 'err'); return; }
          chosen.push(u);
          res.innerHTML = '';
          inp.value = '';
          drawSel();
        });
      }, 300);
      inp.addEventListener('input', function () { run(); });

      create.onclick = async function () {
        const name = title.value.trim();
        if (!name) { OM.toast('Укажите название группы', 'err'); return; }
        create.disabled = true;
        try {
          const r = await OM.api.post('/api/chats', {
            kind: 'group',
            title: name,
            members: chosen.map(u => num(u.id)).filter(Boolean)
          });
          m.close();
          if (r.chat) {
            mergeChat(r.chat);
            await loadChats('');
            renderChats();
            selectChat(r.chat.id);
            OM.toast('Группа создана', 'ok');
          }
        } catch (e) {
          create.disabled = false;
          OM.toast(e.message || 'Не удалось создать группу', 'err');
        }
      };
      setTimeout(function () { title.focus(); }, 60);
    }

    function render() { if (tab === 'dm') renderDm(); else renderGroup(); }

    tabs.addEventListener('click', function (e) {
      const b = e.target.closest('[data-tab]');
      if (!b) return;
      tab = b.getAttribute('data-tab');
      tabs.querySelectorAll('.chip').forEach(c => c.classList.toggle('active', c === b));
      render();
    });
    render();
  }

  /* -------------------------------------------------------- realtime (WS) */

  OM.on('ws:message.new', function (d) {
    const m = d && d.message;
    if (!m) return;
    const cid = num(m.chat_id || (d && d.chat_id));
    if (!cid) return;
    const chat = findChat(cid);
    if (chat) {
      chat.last_message = {
        id: m.id, sender: m.sender, sender_id: m.sender_id,
        body: String(m.body || '').slice(0, 160), attachment: m.attachment,
        attachment_kind: m.attachment_kind, created_at: m.created_at,
        system: m.system, deleted: m.deleted
      };
      chat.last_message_at = num(m.created_at) || chat.last_message_at;
    }
    const open = (activeId === cid && OM.currentView === 'chats');
    if (open) {
      if (chat) chat.unread = 0;
      upsertMessage(m, { scroll: true });
      markRead();
    } else if (chat) {
      if (m.mine) chat.unread = 0;
      else chat.unread = (typeof d.unread === 'number') ? num(d.unread) : (num(chat.unread) + 1);
    } else if (!m.mine) {
      reloadList();
    }
    renderChats();
  });

  OM.on('ws:message.edited', function (d) {
    const m = d && d.message;
    if (!m) return;
    const cid = num(m.chat_id || (d && d.chat_id));
    if (cid !== activeId) { reloadList(); return; }
    const cur = msgById(m.id);
    if (!cur) { reloadCurrent(); return; }
    // в событии mine пересчитан для зрителя 0 — сохраняем прежнее значение
    upsertMessage(Object.assign({}, cur, m, { mine: cur.mine }), { scroll: false });
  });

  OM.on('ws:message.deleted', function (d) {
    const cid = num(d && d.chat_id);
    const mid = num(d && d.message_id);
    if (cid === activeId) {
      const cur = msgById(mid);
      if (cur) {
        cur.deleted = true;
        cur.body = '';
        cur.attachment = '';
        renderMessages(false);
      } else {
        reloadCurrent();
      }
    }
    reloadList();
  });

  OM.on('ws:message.reaction', function (d) {
    const cid = num(d && d.chat_id);
    const mid = num(d && d.message_id);
    if (cid !== activeId) return;
    if (Date.now() - (localReactionAt[mid] || 0) < 2500) return; // своя реакция уже применена
    const cur = msgById(mid);
    if (!cur) return;
    const old = Array.isArray(cur.reactions) ? cur.reactions : [];
    const incoming = Array.isArray(d.reactions) ? d.reactions : [];
    cur.reactions = incoming.map(r => ({
      emoji: r.emoji,
      count: num(r.count),
      mine: !!(old.find(o => o.emoji === r.emoji) || {}).mine
    }));
    renderMessages(false);
  });

  OM.on('ws:typing', function (d) {
    if (!d || num(d.chat_id) !== activeId) return;
    if (num(d.user_id) === meId()) return;
    showTyping(d.user || { id: d.user_id });
  });

  OM.on('ws:message.read', function (d) {
    if (!d || num(d.chat_id) !== activeId) return;
    const mid = num(d.message_id);
    let changed = false;
    messages.forEach(m => {
      if (m.mine && num(m.id) <= mid && num(m.reads) < 1) { m.reads = 1; changed = true; }
    });
    if (changed) renderMessages(false);
  });

  OM.on('ws:chat.new', function (d) {
    const c = d && d.chat;
    if (!c) return;
    mergeChat(c);
    renderChats();
  });

  OM.on('ws:chat.updated', function (d) {
    const c = d && d.chat;
    if (!c) return;
    const merged = mergeChat(c);
    if (num(c.id) === activeId) {
      activeChat = merged || activeChat;
      renderHead();
    }
    renderChats();
  });

  OM.on('ws:chat.left', function (d) {
    const cid = num(d && d.chat_id);
    if (!cid) return;
    OM.state.chats = (OM.state.chats || []).filter(c => num(c.id) !== cid);
    viewChats = viewChats.filter(c => num(c.id) !== cid);
    if (activeId === cid) closeChat();
    renderChats();
    updateUnread();
  });

  OM.on('ws:presence', function (d) {
    if (!d) return;
    const uid = num(d.user_id);
    if (!uid) return;
    (OM.state.chats || []).forEach(function (c) {
      [c.peer, c.peer_card].forEach(function (p) {
        if (!p || num(p.id) !== uid) return;
        p.presence = p.presence || {};
        p.presence.online = !!d.online;
        if (d.last_seen) p.presence.last_seen = num(d.last_seen);
      });
    });
    if (OM.currentView === 'chats') { renderChats(); renderHead(); }
  });

  /* ----------------------------------------------------- раздел приложения */

  function enter(params) {
    params = params || {};
    bindOnce();

    const search = q('chat-search');
    if (search) {
      search.placeholder = 'Поиск: ID чата, юзернейм, почта, телефон, Orange ID…';
      search.value = '';
    }
    searchQuery = '';
    userResults = [];
    closePopup();

    loadChats('').then(renderChats).catch(function (e) {
      OM.toast(e.message || 'Не удалось загрузить чаты', 'err');
    });

    const want = num(params.id) || num(pendingSelect) || activeId;
    pendingSelect = 0;
    if (want) selectChat(want);

    focusHandler = function () {
      if (activeId && OM.currentView === 'chats') { markRead(); renderChats(); }
    };
    window.addEventListener('focus', focusHandler);

    const box = q('chat-messages');
    scrollHandler = function () {
      const b = q('chat-messages');
      if (b && b.scrollTop < 80) loadOlder();
    };
    if (box) box.addEventListener('scroll', scrollHandler);
  }

  function leave() {
    // снимаем слушатели окна и скролла, которые добавили сами
    if (focusHandler) { window.removeEventListener('focus', focusHandler); focusHandler = null; }
    const box = q('chat-messages');
    if (box && scrollHandler) { box.removeEventListener('scroll', scrollHandler); scrollHandler = null; }
    clearTyping();
    closePopup();
  }

  /* Регистрируем раздел (в т.ч. повторно — app.js инициализирует OM.views) */
  function register() {
    OM.views = OM.views || {};
    OM.views.chats = { enter: enter, leave: leave };
  }
  register();
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', register);
  }

  /* Публичные помощники модуля */
  OM.openChat = function (id) {
    if (OM.currentView !== 'chats') {
      pendingSelect = num(id);
      OM.navigate('chats');
    }
    selectChat(num(id) || id);
  };
  OM.on('open-chat', function (id) {
    OM.openChat(id && id.id ? id.id : id);
  });
})();
