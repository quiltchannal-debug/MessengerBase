/* OrangeM — оболочка приложения: навигация, запуск, флеш-ключи в реальном времени */
window.OM = window.OM || {};

OM.views = OM.views || {};        // реестр разделов: {enter(params), leave()} — модули грузятся раньше
OM.currentView = '';

/* ------------------------------------------------------------------ IndexedDB: хранение дескрипторов файлов флеш-ключей */
OM.idb = (function () {
  function open() {
    return new Promise((res, rej) => {
      const r = indexedDB.open('orangem', 1);
      r.onupgradeneeded = () => { r.result.createObjectStore('kv'); };
      r.onsuccess = () => res(r.result);
      r.onerror = () => rej(r.error);
    });
  }
  async function set(k, v) {
    const db = await open();
    return new Promise((res, rej) => {
      const t = db.transaction('kv', 'readwrite');
      t.objectStore('kv').put(v, k);
      t.oncomplete = () => res(true);
      t.onerror = () => rej(t.error);
    });
  }
  async function get(k) {
    const db = await open();
    return new Promise((res, rej) => {
      const t = db.transaction('kv', 'readonly');
      const q = t.objectStore('kv').get(k);
      q.onsuccess = () => res(q.result);
      q.onerror = () => rej(q.error);
    });
  }
  async function del(k) {
    const db = await open();
    return new Promise((res) => {
      const t = db.transaction('kv', 'readwrite');
      t.objectStore('kv').delete(k);
      t.oncomplete = () => res(true);
    });
  }
  return { set, get, del };
})();

/* ------------------------------------------------------------------ флеш-ключи (File System Access API) */
OM.flashKey = (function () {
  const supported = typeof window.showSaveFilePicker === 'function';

  function parseUuid(content) {
    const m = /(^|\n)uuid:\s*([A-Za-z0-9]+)/.exec(content || '');
    return m ? m[2] : '';
  }

  async function attachHandle(uuid, handle) {
    if (!uuid || !handle) return;
    try { await OM.idb.set('keyfile:' + uuid, handle); } catch (e) {}
    _handles[uuid] = handle;
  }
  const _handles = {};

  async function getHandle(uuid) {
    if (_handles[uuid]) return _handles[uuid];
    try {
      const h = await OM.idb.get('keyfile:' + uuid);
      if (h) { _handles[uuid] = h; return h; }
    } catch (e) {}
    return null;
  }

  /** Сохранить новый ключ на носитель (возвращает uuid) */
  async function save(content, suggestedName) {
    const uuid = parseUuid(content);
    if (!supported) {
      OM.download(suggestedName || 'orangem-key.omkey', content, 'text/plain');
      return uuid;
    }
    try {
      const handle = await window.showSaveFilePicker({
        suggestedName: suggestedName || 'orangem-key.omkey',
        types: [{ description: 'OrangeM session key', accept: { 'text/plain': ['.omkey'] } }]
      });
      await writeTo(handle, content);
      await attachHandle(uuid, handle);
      OM.toast('Ключ сохранён и будет обновляться в реальном времени', 'ok');
    } catch (e) {
      if (e && e.name === 'AbortError') throw e;
      OM.download(suggestedName || 'orangem-key.omkey', content, 'text/plain');
    }
    return uuid;
  }

  /** Выбрать существующий файл ключа и прочитать его */
  async function pick() {
    if (!supported) {
      return new Promise((res, rej) => {
        const inp = document.createElement('input');
        inp.type = 'file';
        inp.accept = '.omkey,.txt';
        inp.onchange = () => {
          const f = inp.files[0];
          if (!f) return rej(new Error('Файл не выбран'));
          const r = new FileReader();
          r.onload = () => res({ content: String(r.result), name: f.name });
          r.onerror = () => rej(new Error('Не удалось прочитать файл'));
          r.readAsText(f);
        };
        inp.click();
      });
    }
    const [handle] = await window.showOpenFilePicker({
      types: [{ description: 'OrangeM session key', accept: { 'text/plain': ['.omkey', '.txt'] } }]
    });
    const file = await handle.getFile();
    const content = await file.text();
    await attachHandle(parseUuid(content), handle);
    return { content, name: file.name, handle };
  }

  async function writeTo(handle, content) {
    const w = await handle.createWritable();
    await w.write(content);
    await w.close();
  }

  /** Перезапись файла на носителе (реальное время) */
  async function rewrite(uuid, content) {
    const h = await getHandle(uuid);
    if (!h) return false;
    try {
      const perm = h.queryPermission ? await h.queryPermission({ mode: 'readwrite' }) : 'granted';
      if (perm !== 'granted' && h.requestPermission) {
        const p = await h.requestPermission({ mode: 'readwrite' });
        if (p !== 'granted') return false;
      }
      await writeTo(h, content);
      return true;
    } catch (e) {
      return false;
    }
  }

  return { supported, save, pick, rewrite, parseUuid };
})();

/* Реальное время: сервер перешифровал файл-ключ — записываем его на носитель */
OM.on('ws:session_file.update', async d => {
  if (!d || !d.uuid || !d.content) return;
  const ok = await OM.flashKey.rewrite(d.uuid, d.content);
  if (ok) {
    localStorage.setItem('orangem_key_sync', String(Date.now()));
    OM.emit('flash-key-updated', d);
  }
});

/* ------------------------------------------------------------------ навигация */
OM.navigate = function (view, params) {
  const target = document.getElementById('view-' + view);
  if (!target) return;
  if (OM.currentView && OM.views[OM.currentView] && OM.views[OM.currentView].leave) {
    try { OM.views[OM.currentView].leave(); } catch (e) {}
  }
  document.querySelectorAll('.main > .view').forEach(v => v.classList.add('hidden'));
  target.classList.remove('hidden');
  OM.currentView = view;
  document.querySelectorAll('.nav-btn').forEach(b => b.classList.toggle('active', b.dataset.view === view));
  if (OM.views[view] && OM.views[view].enter) {
    try { OM.views[view].enter(params || {}); } catch (e) { console.error(e); }
  }
  window.scrollTo(0, 0);
  if (location.hash !== '#' + view) history.replaceState(null, '', '#' + view);
};

document.addEventListener('click', e => {
  const nav = e.target.closest('.nav-btn');
  if (nav && nav.dataset.view) OM.navigate(nav.dataset.view);
});

/* ------------------------------------------------------------------ запуск */
OM.showAuth = function () {
  document.getElementById('boot').classList.add('hidden');
  document.getElementById('app').classList.add('hidden');
  document.getElementById('view-auth').classList.remove('hidden');
  if (OM.views.auth && OM.views.auth.enter) OM.views.auth.enter({});
};

OM.showApp = function () {
  document.getElementById('view-auth').classList.add('hidden');
  document.getElementById('app').classList.remove('hidden');
  renderSideMe();
  const initial = (location.hash || '').replace('#', '') || 'feed';
  const allowed = ['feed', 'chats', 'communities', 'stories', 'settings', 'profile'];
  OM.navigate(allowed.indexOf(initial) >= 0 ? initial : 'feed');
};

function renderSideMe() {
  const box = document.getElementById('side-me');
  const me = OM.state.me;
  if (!me) { box.innerHTML = ''; return; }
  box.innerHTML = `
    ${OM.avatar(me, { size: 'sm', online: true })}
    <div class="meta">
      <div class="nm">${OM.esc(me.display_name || me.username)}</div>
      <div class="id">${OM.esc(me.orange_id || '')}</div>
    </div>`;
  box.onclick = () => OM.navigate('profile', { id: me.id });
}

OM.setMe = function (me) {
  OM.state.me = me;
  renderSideMe();
  localStorage.setItem('orangem_me', JSON.stringify({ id: me.id, username: me.username }));
};

OM.onUnauthorized = function () {
  OM.api.setToken('');
  OM.ws.disconnect();
  OM.state.me = null;
  OM.showAuth();
};

OM.logout = async function () {
  try { await OM.api.post('/api/auth/logout'); } catch (e) {}
  OM.api.setToken('');
  OM.ws.disconnect();
  OM.state.me = null;
  OM.showAuth();
};

OM.refreshMe = async function () {
  const r = await OM.api.get('/api/me');
  OM.setMe(r.user);
  return r.user;
};

function refreshBootSub(text) {
  const el = document.querySelector('.boot-sub');
  if (el) el.textContent = text;
}

async function boot() {
  refreshBootSub('проверка сессии…');
  if (!OM.api.hasToken()) { OM.showAuth(); return; }
  try {
    const r = await OM.api.get('/api/me');
    OM.setMe(r.user);
    OM.ws.connect();
    OM.showApp();
  } catch (e) {
    if (e.status === 401) { OM.showAuth(); return; }
    refreshBootSub('сервер недоступен, повтор…');
    setTimeout(boot, 2500);
  }
}

window.addEventListener('DOMContentLoaded', boot);

/* Обновление бейджа непрочитанных чатов */
OM.updateChatBadge = function (n) {
  const b = document.getElementById('nav-badge-chats');
  if (!b) return;
  if (n > 0) b.textContent = n > 99 ? '99+' : String(n);
  else b.textContent = '';
};

/* Уведомления в реальном времени */
OM.on('ws:notification', d => {
  const n = d.notification || {};
  if (n.kind === 'message') return; // обрабатывается в chats.js
  const who = n.actor ? '' : '';
  OM.toast((n.text || 'Новое уведомление') + who, 'ok');
});
