/* OrangeM — настройки: профиль, конфиденциальность, безопасность (2FA, пароль,
   сессии, флеш-ключи), внешний вид и админка.
   Регистрирует OM.views.settings (#settings-body), навигация — #settings-nav .chip[data-tab]. */
window.OM = window.OM || {};

(function () {
  'use strict';

  /* ======================================================================= состояние */
  const TABS = ['profile', 'privacy', 'security', 'appearance', 'sessions', 'admin'];
  let current = 'profile';
  let navBound = false;

  const $ = id => document.getElementById(id);
  const esc = s => OM.esc(s);

  /* Акцентные темы: значения CSS-переменных из css/style.css */
  const ACCENTS = {
    orange: {
      '--orange': '#ff7a1a', '--orange-2': '#ff9f45', '--orange-dim': 'rgba(255,122,26,.14)',
      '--blue': '#2f7ce0', '--blue-2': '#1e5aa8'
    },
    blue: {
      '--orange': '#2f7ce0', '--orange-2': '#66a6f0', '--orange-dim': 'rgba(47,124,224,.16)',
      '--blue': '#ff7a1a', '--blue-2': '#c2540c'
    },
    gradient: {
      '--orange': '#ff7a1a', '--orange-2': '#ff9f45', '--orange-dim': 'rgba(255,122,26,.14)',
      '--blue': '#2f7ce0', '--blue-2': '#1e5aa8'
    }
  };

  const EVERYONE = [['everyone', 'Все'], ['contacts', 'Контакты'], ['nobody', 'Никто']];
  const STORIES_OPTS = [['everyone', 'Все'], ['contacts', 'Контакты'], ['followers', 'Подписчики'], ['nobody', 'Никто']];

  const PRESENCE_LABEL = {
    online: 'В сети',
    offline: 'Не в сети',
    invisible: 'Невидимка'
  };

  const SESSION_KIND = {
    password: 'Вход по паролю',
    email_register: 'Регистрация (почта)',
    phone_register: 'Регистрация (телефон)',
    otp_email: 'Код на почту',
    otp_phone: 'Код в SMS',
    otp: 'Вход по коду',
    totp: 'Аутентификатор',
    password_reset: 'Сброс пароля',
    session_file: 'Флеш-ключ'
  };

  function settings() {
    return (OM.state.me && OM.state.me.settings) || {};
  }

  /** Локальное применение темы/компактного режима (в т.ч. до ответа сервера) */
  function applyAppearance() {
    const s = settings();
    const accent = s.accent || localStorage.getItem('orangem_accent') || 'orange';
    applyAccent(accent);
    const compact = s.compact !== undefined ? !!s.compact : localStorage.getItem('orangem_compact') === '1';
    document.body.classList.toggle('compact', compact);
  }

  function applyAccent(name) {
    const vars = ACCENTS[name] || ACCENTS.orange;
    const root = document.documentElement;
    Object.keys(vars).forEach(k => root.style.setProperty(k, vars[k]));
    try { localStorage.setItem('orangem_accent', name); } catch (e) {}
  }

  /** Мягкое сохранение настроек: PATCH /api/users/me/settings {settings:{...}} */
  async function saveSettings(patch) {
    const me = OM.state.me || {};
    me.settings = Object.assign({}, me.settings || {}, patch);
    try {
      const r = await OM.api.patch('/api/users/me/settings', { settings: patch });
      if (r && r.settings) me.settings = r.settings;
    } catch (e) {
      OM.toast(e.message || 'Не удалось сохранить настройки', 'err');
    }
  }

  function pickFile(accept) {
    return new Promise(resolve => {
      const inp = document.createElement('input');
      inp.type = 'file';
      if (accept) inp.accept = accept;
      inp.onchange = () => resolve((inp.files && inp.files[0]) || null);
      inp.click();
    });
  }

  function verifiedBadge(ok) {
    return ok ? '<span class="badge orange">подтверждён</span>' : '<span class="badge grey">не подтверждён</span>';
  }

  function sel(label, id, value, options) {
    return `<div class="field"><label>${esc(label)}</label><select id="${id}">` +
      options.map(o => `<option value="${o[0]}"${value === o[0] ? ' selected' : ''}>${esc(o[1])}</option>`).join('') +
      '</select></div>';
  }

  /* ======================================================================= навигация */
  function setTab(tab) {
    if (TABS.indexOf(tab) < 0) return;
    current = tab;
    document.querySelectorAll('#settings-nav .chip').forEach(ch => {
      ch.classList.toggle('active', ch.dataset.tab === tab);
    });
  }

  function syncAdminChip() {
    const chip = document.querySelector('#settings-nav .chip[data-tab="admin"]');
    const isAdmin = !!(OM.state.me && OM.state.me.is_admin);
    if (chip) chip.classList.toggle('hidden', !isAdmin);
    return isAdmin;
  }

  function bindNav() {
    if (navBound) return;
    navBound = true;
    const nav = $('settings-nav');
    if (!nav) return;
    nav.addEventListener('click', e => {
      const chip = e.target.closest('.chip[data-tab]');
      if (!chip) return;
      const tab = chip.dataset.tab;
      if (tab === 'admin' && !(OM.state.me && OM.state.me.is_admin)) {
        OM.toast('Раздел доступен только администраторам', 'err');
        return;
      }
      setTab(tab);
      render();
    });
  }

  async function render() {
    const body = $('settings-body');
    if (!body) return;
    syncAdminChip();
    const tabAtStart = current;
    body.innerHTML = '<div class="panel">Загрузка…</div>';
    try {
      if (current === 'profile') await renderProfile(body);
      else if (current === 'privacy') await renderPrivacy(body);
      else if (current === 'security') await renderSecurity(body);
      else if (current === 'appearance') await renderAppearance(body);
      else if (current === 'sessions') await renderSessions(body);
      else if (current === 'admin') await renderAdmin(body);
      else body.innerHTML = '';
    } catch (e) {
      body.innerHTML = `<div class="panel"><h3>Ошибка</h3><div class="err">${esc(e.message || 'Не удалось открыть раздел')}</div></div>`;
    }
    // Если пользователь переключил вкладку, пока грузились данные, рисуем актуальную,
    // иначе на экране останется содержимое старой вкладки.
    if (tabAtStart !== current) return render();
  }

  /* ======================================================================= профиль */
  async function renderProfile(body) {
    const me = OM.state.me || {};
    body.innerHTML = `
    <div class="panel">
      <h3>Профиль</h3>
      <div class="desc">Имя, логин, описание и изображения видны другим пользователям OrangeM.</div>
      <div class="form-row" style="align-items:center">
        ${OM.avatar(me, { size: 'lg' })}
        <button class="btn ghost" data-pact="upload-avatar">Загрузить аватар</button>
        <button class="btn ghost" data-pact="upload-banner">Загрузить баннер</button>
      </div>
      ${me.banner ? `<div class="post-media" style="margin-top:12px"><img src="${esc(me.banner)}" alt=""></div>` : ''}
      <div class="field" style="margin-top:12px"><label>Отображаемое имя</label>
        <input class="input" id="st-name" maxlength="64" value="${esc(me.display_name || '')}"></div>
      <div class="field"><label>Логин</label>
        <input class="input" id="st-username" maxlength="32" value="${esc(me.username || '')}">
        <div class="hint">3–32 символа: латиница, цифры, «_» и «.»</div></div>
      <div class="field"><label>О себе</label>
        <textarea id="st-bio" maxlength="500">${esc(me.bio || '')}</textarea>
        <div class="hint"><span id="st-bio-count">0</span> / 500</div></div>
      <div class="field"><label>Режим присутствия</label>
        <select id="st-presence">
          <option value="online">В сети — показывать активность</option>
          <option value="offline">Не в сети — не показывать активность</option>
          <option value="invisible">Невидимка — видеть других, оставаясь незаметным</option>
        </select></div>
      <button class="btn primary" data-pact="save-profile">Сохранить профиль</button>
      <div class="err hidden" id="st-prof-err"></div>
      <div class="okmsg hidden" id="st-prof-ok">Профиль сохранён</div>
    </div>
    <div class="panel">
      <h3>Orange ID</h3>
      <div class="desc">Единый идентификатор OrangeM — работает во всех сервисах и для входа.</div>
      <div class="kv"><span class="k">Orange ID</span><b>${esc(me.orange_id || '—')}</b>
        <button class="btn small ghost" data-pact="copy-oid">Скопировать</button></div>
      <div class="kv"><span class="k">Короткий ID</span><b>${esc(me.short_id || '—')}</b>
        <button class="btn small ghost" data-pact="copy-sid">Скопировать</button></div>
      <div class="kv"><span class="k">Логин</span><b>@${esc(me.username || '')}</b></div>
    </div>
    <div class="panel">
      <h3>Контакты и вход</h3>
      <div class="desc">Подтверждённые контакты используются для входа и восстановления доступа.</div>
      <div class="kv"><span class="k">E-mail</span><b>${esc(me.email || 'не указан')}</b>
        ${me.email ? verifiedBadge(me.email_verified) : ''}</div>
      <div class="kv"><span class="k">Телефон</span><b>${esc(me.phone || 'не указан')}</b>
        ${me.phone ? verifiedBadge(me.phone_verified) : ''}</div>
      <div class="switch" style="margin-top:12px">
        <div class="lb">Двухфакторная аутентификация<small>вход по коду из приложения-аутентификатора</small></div>
        ${me.totp_enabled ? '<span class="badge orange">включена</span>' : '<span class="badge grey">выключена</span>'}
      </div>
      <div class="switch">
        <div class="lb">Режим присутствия<small>как вас видят другие пользователи</small></div>
        <span class="badge grey">${esc(PRESENCE_LABEL[me.presence_mode] || 'В сети')}</span>
      </div>
      <button class="btn danger" data-pact="logout" style="margin-top:14px">Выйти из аккаунта</button>
    </div>`;

    const bio = $('st-bio');
    const cnt = $('st-bio-count');
    const upd = () => { cnt.textContent = String(bio.value.length); };
    bio.addEventListener('input', upd);
    upd();
    $('st-presence').value = me.presence_mode || 'online';

    body.querySelectorAll('[data-pact]').forEach(b => {
      b.onclick = () => profileAction(b.dataset.pact);
    });
  }

  async function profileAction(act) {
    const me = OM.state.me || {};
    if (act === 'copy-oid') { OM.copy(me.orange_id || ''); return; }
    if (act === 'copy-sid') { OM.copy(String(me.short_id || '')); return; }
    if (act === 'logout') { OM.confirm('Выйти из аккаунта на этом устройстве?', () => OM.logout(), 'Выйти'); return; }

    if (act === 'upload-avatar' || act === 'upload-banner') {
      const file = await pickFile('image/*');
      if (!file) return;
      try {
        const up = await OM.api.upload(file, 'image');
        const payload = {};
        payload[act === 'upload-avatar' ? 'avatar' : 'banner'] = up.url;
        const r = await OM.api.patch('/api/users/me', payload);
        OM.setMe(r.user);
        OM.toast('Изображение обновлено', 'ok');
        render();
      } catch (e) {
        OM.toast(e.message || 'Не удалось загрузить изображение', 'err');
      }
      return;
    }

    if (act === 'save-profile') {
      const err = $('st-prof-err');
      const ok = $('st-prof-ok');
      err.classList.add('hidden');
      ok.classList.add('hidden');
      const payload = {};
      const name = $('st-name').value.trim();
      const username = $('st-username').value.trim();
      const bio = $('st-bio').value;
      const presence = $('st-presence').value;
      if (name !== (me.display_name || '')) payload.display_name = name;
      if (username !== (me.username || '')) payload.username = username;
      if (bio !== (me.bio || '')) payload.bio = bio;
      if (presence !== (me.presence_mode || 'online')) payload.presence = presence;
      if (!Object.keys(payload).length) { OM.toast('Изменений нет'); return; }
      try {
        const r = await OM.api.patch('/api/users/me', payload);
        OM.setMe(r.user);
        OM.toast('Профиль сохранён', 'ok');
        render();
      } catch (e) {
        err.textContent = e.message || 'Не удалось сохранить профиль';
        err.classList.remove('hidden');
      }
    }
  }

  /* ======================================================================= конфиденциальность */
  async function renderPrivacy(body) {
    const me = OM.state.me || {};
    const p = me.privacy || {};
    body.innerHTML = `
    <div class="panel">
      <h3>Кто может видеть и писать</h3>
      <div class="desc">Изменения применяются сразу после сохранения.</div>
      ${sel('Личные сообщения', 'pv-dm', p.dm || 'everyone', EVERYONE)}
      ${sel('Истории', 'pv-stories', p.stories || 'everyone', STORIES_OPTS)}
      ${sel('Время последнего визита', 'pv-last-seen', p.last_seen || 'everyone', EVERYONE)}
      ${sel('Статус «в сети»', 'pv-online', p.online || 'everyone', EVERYONE)}
      ${sel('Просмотр профиля', 'pv-profile', p.profile || 'everyone', EVERYONE)}
      <div class="switch"><div class="lb">Отчёты о прочтении<small>показывать собеседникам, что вы прочитали сообщение</small></div>
        <button class="toggle${p.read_receipts ? ' on' : ''}" data-priv="read_receipts"></button></div>
      <div class="switch"><div class="lb">Поиск по телефону<small>находить вас по номеру телефона</small></div>
        <button class="toggle${p.find_phone ? ' on' : ''}" data-priv="find_phone"></button></div>
      <div class="switch"><div class="lb">Поиск по e-mail<small>находить вас по адресу почты</small></div>
        <button class="toggle${p.find_email ? ' on' : ''}" data-priv="find_email"></button></div>
      <button class="btn primary" data-pact="save-privacy" style="margin-top:14px">Сохранить</button>
      <div class="err hidden" id="pv-err"></div>
    </div>
    <div class="panel">
      <h3>Заблокированные</h3>
      <div class="desc">Заблокированные пользователи не могут писать вам и видеть ваш профиль.</div>
      <div id="pv-blocked"><div class="hint">Загрузка…</div></div>
    </div>`;

    body.querySelectorAll('.toggle[data-priv]').forEach(t => {
      t.onclick = () => t.classList.toggle('on');
    });
    body.querySelector('[data-pact="save-privacy"]').onclick = savePrivacy;
    loadBlocked();
  }

  async function savePrivacy() {
    const err = $('pv-err');
    err.classList.add('hidden');
    const on = key => {
      const t = document.querySelector('.toggle[data-priv="' + key + '"]');
      return !!(t && t.classList.contains('on'));
    };
    const payload = {
      dm: $('pv-dm').value,
      stories: $('pv-stories').value,
      last_seen: $('pv-last-seen').value,
      online: $('pv-online').value,
      profile: $('pv-profile').value,
      read_receipts: on('read_receipts'),
      find_phone: on('find_phone'),
      find_email: on('find_email')
    };
    try {
      const r = await OM.api.patch('/api/users/me/privacy', payload);
      OM.setMe(r.user);
      OM.toast('Настройки конфиденциальности сохранены', 'ok');
    } catch (e) {
      err.textContent = e.message || 'Не удалось сохранить настройки';
      err.classList.remove('hidden');
    }
  }

  async function loadBlocked() {
    const box = $('pv-blocked');
    if (!box) return;
    try {
      const r = await OM.api.get('/api/users/me/blocked');
      const users = r.users || [];
      box.innerHTML = users.length
        ? users.map(u => `<div class="member-row">
            ${OM.avatar(u, { size: 'sm' })}
            <div class="cw" style="flex:1;min-width:0">
              <div class="comm-name" style="font-size:14px">${esc(u.display_name || u.username)}</div>
              <div class="hint">@${esc(u.username)} · ${esc(u.orange_id || '')}</div>
            </div>
            <button class="btn small ghost" data-unblock="${u.id}">Разблокировать</button>
          </div>`).join('')
        : '<div class="hint">Список пуст.</div>';
    } catch (e) {
      box.innerHTML = `<div class="err">${esc(e.message || 'Не удалось загрузить список')}</div>`;
    }
  }

  async function unblockUser(id) {
    try {
      await OM.api.del('/api/users/' + id + '/block');
      OM.toast('Пользователь разблокирован', 'ok');
      loadBlocked();
    } catch (e) {
      OM.toast(e.message || 'Не удалось разблокировать', 'err');
    }
  }

  /* ======================================================================= безопасность */
  async function renderSecurity(body) {
    const me = OM.state.me || {};
    body.innerHTML = `
    <div class="panel">
      <h3>Смена пароля</h3>
      <div class="desc">Пароль используется для входа вместе с логином, почтой или телефоном.</div>
      <div class="field"><label>Текущий пароль</label>
        <input class="input" type="password" id="sec-old" autocomplete="current-password"></div>
      <div class="field"><label>Новый пароль</label>
        <input class="input" type="password" id="sec-new" autocomplete="new-password"></div>
      <div class="field"><label>Повторите новый пароль</label>
        <input class="input" type="password" id="sec-rep" autocomplete="new-password"></div>
      <button class="btn primary" data-pact="change-pass">Сменить пароль</button>
      <div class="err hidden" id="sec-err"></div>
      <div class="okmsg hidden" id="sec-ok">Пароль изменён</div>
    </div>
    <div class="panel" id="sec-totp"></div>
    <div class="panel">
      <h3>Все устройства</h3>
      <div class="desc">Завершает все сессии, включая текущую, — после этого потребуется войти заново.
        Отдельные сессии можно завершить на вкладке «Устройства и флеш-ключи».</div>
      <button class="btn danger" data-pact="logout-all">Выйти на всех устройствах</button>
    </div>`;

    body.querySelector('[data-pact="change-pass"]').onclick = changePassword;
    body.querySelector('[data-pact="logout-all"]').onclick = logoutAll;
    renderTotp($('sec-totp'), me);
  }

  async function changePassword() {
    const err = $('sec-err');
    const ok = $('sec-ok');
    err.classList.add('hidden');
    ok.classList.add('hidden');
    const oldP = $('sec-old').value;
    const newP = $('sec-new').value;
    const rep = $('sec-rep').value;
    if (!oldP) { err.textContent = 'Введите текущий пароль'; err.classList.remove('hidden'); return; }
    if (newP.length < 6) { err.textContent = 'Новый пароль: минимум 6 символов'; err.classList.remove('hidden'); return; }
    if (newP !== rep) { err.textContent = 'Пароли не совпадают'; err.classList.remove('hidden'); return; }
    try {
      await OM.api.post('/api/auth/password/change', { old_password: oldP, new_password: newP });
      $('sec-old').value = $('sec-new').value = $('sec-rep').value = '';
      ok.classList.remove('hidden');
      OM.toast('Пароль изменён', 'ok');
    } catch (e) {
      err.textContent = e.message || 'Не удалось сменить пароль';
      err.classList.remove('hidden');
    }
  }

  function logoutAll() {
    OM.confirm('Завершить все сессии, включая текущую?', async () => {
      try {
        await OM.api.post('/api/auth/logout/all', {});
        OM.toast('Все сессии завершены', 'ok');
        OM.logout();
      } catch (e) {
        OM.toast(e.message || 'Не удалось завершить сессии', 'err');
      }
    }, 'Выйти везде');
  }

  /** Группировка секрета по 4 символа для удобного ручного ввода */
  function groupSecret(s) {
    return String(s || '').replace(/(.{4})/g, '$1 ').trim();
  }

  function renderTotp(host, me) {
    if (!host) return;
    if (me.totp_enabled) {
      host.innerHTML = `
        <h3>Двухфакторная аутентификация</h3>
        <div class="desc">Защита включена. При входе потребуется 6-значный код из приложения-аутентификатора.</div>
        <div class="switch"><div class="lb">Статус<small>код запрашивается при каждом входе</small></div>
          <span class="badge orange">включена</span></div>
        <div class="field" style="margin-top:12px"><label>Код из приложения</label>
          <input class="input" id="sec-totp-code" maxlength="6" inputmode="numeric" placeholder="000000"></div>
        <button class="btn danger" data-pact="totp-disable">Отключить 2FA</button>
        <div class="err hidden" id="sec-totp-err"></div>`;
      host.querySelector('[data-pact="totp-disable"]').onclick = totpDisable;
      return;
    }
    host.innerHTML = `
      <h3>Двухфакторная аутентификация</h3>
      <div class="desc">Подключите приложение-аутентификатор (Google Authenticator, Aegis, 1Password)
        и подтвердите вход 6-значным кодом.</div>
      <button class="btn primary" data-pact="totp-setup">Включить двухфакторную аутентификацию</button>`;
    host.querySelector('[data-pact="totp-setup"]').onclick = () => totpSetup(host);
  }

  async function totpSetup(host) {
    host.innerHTML = '<div class="hint">Готовим секретный ключ…</div>';
    let r;
    try {
      r = await OM.api.post('/api/auth/totp/setup', {});
    } catch (e) {
      host.innerHTML = `<div class="err">${esc(e.message || 'Не удалось начать настройку')}</div>`;
      return;
    }

    // qrcode.js пишет другой агент — используем OM.qr.svg только если он есть
    let qr = '';
    try {
      if (window.OM && OM.qr && typeof OM.qr.svg === 'function') qr = OM.qr.svg(r.uri, 190) || '';
    } catch (e) {
      qr = '';
    }

    host.innerHTML = `
      <h3>Двухфакторная аутентификация</h3>
      <div class="desc">Отсканируйте QR-код или введите ключ вручную, затем введите 6-значный код из приложения.</div>
      ${qr ? `<div class="qr-box">${qr}</div>` : '<div class="hint">QR-код недоступен — добавьте ключ вручную по ссылке ниже.</div>'}
      <div class="field" style="margin-top:14px"><label>Секретный ключ</label>
        <div class="secret">${esc(groupSecret(r.secret))}</div></div>
      <div class="form-row">
        <button class="btn small ghost" data-pact="copy-secret">Скопировать ключ</button>
        <button class="btn small ghost" data-pact="copy-uri">Скопировать ссылку</button>
      </div>
      <div class="hint">Ручной ввод: издатель <b>OrangeM</b>, аккаунт <b>${esc(r.orange_id || '')}</b>,
        ключ <b>${esc(r.secret)}</b>, тип «по времени», 6 цифр, период 30 секунд.</div>
      <div class="hint">Ссылка для ручного добавления: ${esc(r.uri)}</div>
      <div class="field" style="margin-top:12px"><label>Код из приложения</label>
        <input class="input" id="sec-totp-code" maxlength="6" inputmode="numeric" placeholder="000000"></div>
      <button class="btn primary" data-pact="totp-enable">Включить</button>
      <div class="err hidden" id="sec-totp-err"></div>`;

    host.querySelector('[data-pact="copy-secret"]').onclick = () => OM.copy(r.secret);
    host.querySelector('[data-pact="copy-uri"]').onclick = () => OM.copy(r.uri);
    host.querySelector('[data-pact="totp-enable"]').onclick = totpEnable;
  }

  async function totpEnable() {
    const err = $('sec-totp-err');
    const code = ($('sec-totp-code').value || '').trim();
    err.classList.add('hidden');
    if (!/^\d{6}$/.test(code)) {
      err.textContent = 'Введите 6-значный код';
      err.classList.remove('hidden');
      return;
    }
    try {
      await OM.api.post('/api/auth/totp/enable', { code });
      await OM.refreshMe();
      OM.toast('Двухфакторная аутентификация включена', 'ok');
      render();
    } catch (e) {
      err.textContent = e.message || 'Неверный код';
      err.classList.remove('hidden');
    }
  }

  async function totpDisable() {
    const err = $('sec-totp-err');
    const code = ($('sec-totp-code').value || '').trim();
    err.classList.add('hidden');
    if (!/^\d{6}$/.test(code)) {
      err.textContent = 'Введите 6-значный код для отключения';
      err.classList.remove('hidden');
      return;
    }
    try {
      await OM.api.post('/api/auth/totp/disable', { code });
      await OM.refreshMe();
      OM.toast('Двухфакторная аутентификация отключена', 'ok');
      render();
    } catch (e) {
      err.textContent = e.message || 'Неверный код';
      err.classList.remove('hidden');
    }
  }

  /* ======================================================================= внешний вид */
  async function renderAppearance(body) {
    const s = settings();
    const accent = s.accent || localStorage.getItem('orangem_accent') || 'orange';
    const compact = s.compact !== undefined ? !!s.compact : localStorage.getItem('orangem_compact') === '1';
    const lang = s.language || 'ru';
    const sound = s.notif_sound !== false;
    const desktop = !!s.notif_desktop;

    body.innerHTML = `
    <div class="panel">
      <h3>Акцентный цвет</h3>
      <div class="desc">Цвет кнопок, активных пунктов и градиентов. Сохраняется в вашем профиле.</div>
      <div class="head-chips" id="ap-accent">
        <button class="chip${accent === 'orange' ? ' active' : ''}" data-accent="orange">Оранжевый</button>
        <button class="chip${accent === 'blue' ? ' active' : ''}" data-accent="blue">Синий</button>
        <button class="chip${accent === 'gradient' ? ' active' : ''}" data-accent="gradient">Оранжево-синий градиент</button>
      </div>
      <div class="switch"><div class="lb">Компактный режим<small>меньше отступы в списках и лентах</small></div>
        <button class="toggle${compact ? ' on' : ''}" data-aset="compact"></button></div>
    </div>
    <div class="panel">
      <h3>Язык интерфейса</h3>
      <div class="desc">Язык сохраняется вместе с остальными настройками профиля.</div>
      <div class="field"><label>Язык</label>
        <select id="ap-lang">
          <option value="ru"${lang === 'ru' ? ' selected' : ''}>Русский</option>
          <option value="en"${lang === 'en' ? ' selected' : ''}>English</option>
        </select></div>
    </div>
    <div class="panel">
      <h3>Уведомления</h3>
      <div class="desc">Настройки уведомлений хранятся в профиле.</div>
      <div class="switch"><div class="lb">Звук<small>сигнал при новом сообщении</small></div>
        <button class="toggle${sound ? ' on' : ''}" data-aset="notif_sound"></button></div>
      <div class="switch"><div class="lb">Уведомления на рабочем столе<small>системные уведомления браузера</small></div>
        <button class="toggle${desktop ? ' on' : ''}" data-aset="notif_desktop"></button></div>
    </div>`;

    // Акцент: применяем сразу и сохраняем
    body.querySelectorAll('#ap-accent .chip[data-accent]').forEach(chip => {
      chip.onclick = () => {
        const name = chip.dataset.accent;
        body.querySelectorAll('#ap-accent .chip').forEach(c => c.classList.toggle('active', c === chip));
        applyAccent(name);
        saveSettings({ accent: name });
      };
    });

    // Переключатели: локально + сохранение
    body.querySelectorAll('.toggle[data-aset]').forEach(t => {
      t.onclick = () => {
        t.classList.toggle('on');
        const key = t.dataset.aset;
        const val = t.classList.contains('on');
        if (key === 'compact') {
          document.body.classList.toggle('compact', val);
          try { localStorage.setItem('orangem_compact', val ? '1' : '0'); } catch (e) {}
        }
        const patch = {};
        patch[key] = val;
        saveSettings(patch);
      };
    });

    const langSel = $('ap-lang');
    langSel.onchange = () => saveSettings({ language: langSel.value });
  }

  /* ======================================================================= устройства и флеш-ключи */
  async function renderSessions(body) {
    body.innerHTML = '<div class="panel">Загрузка устройств и ключей…</div>';
    const [sess, files, log] = await Promise.all([
      OM.api.get('/api/auth/sessions').catch(() => ({ sessions: [] })),
      OM.api.get('/api/auth/session-files').catch(() => ({ files: [] })),
      OM.api.get('/api/auth/session-files/log').catch(() => ({ events: [] }))
    ]);
    const sessions = sess.sessions || [];
    const keyFiles = files.files || [];
    const events = log.events || [];

    body.innerHTML = `
    <div class="panel">
      <h3>Активные сессии</h3>
      <div class="desc">Все устройства, с которых выполнен вход в ваш аккаунт.</div>
      ${sessions.length ? sessions.map(sessionRow).join('') : '<div class="hint">Активных сессий нет.</div>'}
    </div>
    <div class="panel">
      <h3>Флеш-ключи</h3>
      <div class="desc">Ключ — это файл <b>.omkey</b> на флешке. Сервер перешифровывает файл в реальном времени:
        при каждом использовании новая версия приходит по websocket и записывается в тот же файл на носителе
        (если браузер поддерживает File System Access API).</div>
      ${keyFiles.length ? keyFiles.map(fileRow).join('') : '<div class="hint">Флеш-ключей пока нет.</div>'}
      <button class="btn primary" data-pact="new-key" style="margin-top:12px">Создать новый ключ</button>
      <div class="hint">${OM.flashKey && OM.flashKey.supported
        ? 'Браузер поддерживает прямое обновление файла ключа.'
        : 'Браузер не поддерживает File System Access API: файл будет скачан, обновляйте его вручную.'}</div>
    </div>
    <div class="panel">
      <h3>Журнал флеш-ключей</h3>
      <div class="desc">Последние события входов и обновлений ключей.</div>
      ${events.length ? events.map(eventRow).join('') : '<div class="hint">Событий пока нет.</div>'}
    </div>`;

    body.querySelectorAll('[data-session]').forEach(b => {
      b.onclick = () => revokeSession(b.dataset.session, b);
    });
    body.querySelectorAll('[data-keyfile]').forEach(b => {
      b.onclick = () => revokeKeyFile(b.dataset.keyfile, b);
    });
    body.querySelector('[data-pact="new-key"]').onclick = createKeyFile;
  }

  function sessionRow(s) {
    return `<div class="device-row">
      <div class="cw" style="flex:1;min-width:0">
        <div class="comm-name" style="font-size:14px">${esc(SESSION_KIND[s.kind] || s.kind || 'Сессия')}
          ${s.current ? '<span class="badge orange">текущая</span>' : ''}</div>
        <div class="hint">IP ${esc(s.ip || '—')}${s.device ? ' · ' + esc(s.device) : ''}</div>
        <div class="hint">${esc(s.ua || 'браузер неизвестен')}</div>
        <div class="hint">создана ${esc(OM.fmtDateTime(s.created_at))} · активность ${esc(OM.timeAgo(s.last_seen))}</div>
      </div>
      ${s.current ? '' : `<button class="btn small danger" data-session="${esc(s.id)}">Завершить</button>`}
    </div>`;
  }

  function fileRow(f) {
    return `<div class="device-row">
      <div class="cw" style="flex:1;min-width:0">
        <div class="comm-name" style="font-size:14px">${esc(f.label || 'Флеш-ключ')}
          ${f.revoked ? '<span class="badge grey">отозван</span>' : '<span class="badge">активен</span>'}</div>
        <div class="hint">UUID ${esc(f.uuid || '—')}</div>
        <div class="hint">перешифровок: ${OM.fmtNum(f.rotations)} · последнее использование ${esc(f.last_used ? OM.timeAgo(f.last_used) : 'ещё не использовался')}</div>
        <div class="hint">создан ${esc(OM.fmtDateTime(f.created_at))}</div>
      </div>
      ${f.revoked ? '' : `<button class="btn small danger" data-keyfile="${esc(f.id)}">Отозвать</button>`}
    </div>`;
  }

  function eventRow(e) {
    return `<div class="member-row">
      <div class="cw" style="flex:1;min-width:0">
        <div class="comm-name" style="font-size:14px">${esc(e.action || 'событие')}</div>
        <div class="hint">ключ ${esc(e.uuid || '—')} · IP ${esc(e.ip || '—')}</div>
      </div>
      <span class="hint">${esc(OM.fmtDateTime(e.created_at))}</span>
    </div>`;
  }

  async function revokeSession(id, btn) {
    if (btn) btn.disabled = true;
    try {
      await OM.api.del('/api/auth/sessions/' + encodeURIComponent(id));
      OM.toast('Сессия завершена', 'ok');
      render();
    } catch (e) {
      if (btn) btn.disabled = false;
      OM.toast(e.message || 'Не удалось завершить сессию', 'err');
    }
  }

  async function revokeKeyFile(id, btn) {
    if (btn) btn.disabled = true;
    try {
      await OM.api.del('/api/auth/session-files/' + encodeURIComponent(id));
      OM.toast('Флеш-ключ отозван', 'ok');
      render();
    } catch (e) {
      if (btn) btn.disabled = false;
      OM.toast(e.message || 'Не удалось отозвать ключ', 'err');
    }
  }

  function createKeyFile() {
    OM.prompt({
      title: 'Новый флеш-ключ',
      label: 'Название ключа',
      placeholder: 'Например, Основная флешка',
      ok: 'Создать',
      hint: 'Файл ключа будет сохранён на выбранный носитель.'
    }, async label => {
      const r = await OM.api.post('/api/auth/session-file/create', { label: String(label || '').trim() || 'Флеш-ключ' });
      try {
        await OM.flashKey.save(r.content, r.file_name);
      } catch (e) {
        if (!e || e.name !== 'AbortError') throw e; // отмена выбора файла — не ошибка
      }
      OM.toast('Ключ создан', 'ok');
      OM.modal({
        title: 'Ключ сохранён',
        body: `<p class="desc">${esc(r.hint || 'Сохраните файл на флешку.')}</p>
          <p class="hint">Файл ключа перешифровывается сервером в реальном времени: новая версия приходит
          по websocket и записывается на носитель. Не переименовывайте и не перемещайте файл.</p>`,
        actions: [{ label: 'Понятно', cls: 'primary' }]
      });
      if (current === 'sessions') render(); // обновим список ключей
    });
  }

  /* ======================================================================= админка */
  async function renderAdmin(body) {
    const me = OM.state.me || {};
    if (!me.is_admin) {
      body.innerHTML = `<div class="panel"><h3>Админка</h3>
        <div class="desc">Раздел доступен только администраторам OrangeM.</div></div>`;
      return;
    }
    body.innerHTML = '<div class="panel">Загрузка данных админки…</div>';
    const [stats, outbox, users] = await Promise.all([
      OM.api.get('/api/admin/stats').catch(() => ({})),
      OM.api.get('/api/admin/outbox').catch(() => ({ outbox: [] })),
      OM.api.get('/api/admin/users').catch(() => ({ users: [] }))
    ]);

    const statCards = [
      ['Пользователи', stats.users, 'всего'],
      ['Новые за сутки', stats.users_today, 'регистраций'],
      ['Онлайн', stats.online, 'сейчас'],
      ['Активные сессии', stats.sessions_active, 'сессий'],
      ['Флеш-ключи', stats.session_files, 'активных'],
      ['Сообщения', stats.messages, 'всего'],
      ['Публикации', stats.posts, 'всего'],
      ['Сообщества', stats.communities, 'всего'],
      ['С 2FA', stats.totp_enabled, 'пользователей'],
      ['Демо-коды', stats.dev_codes, 'режим доставки']
    ].map(row => `<div class="card">
        <div class="stat"><b>${OM.fmtNum(row[1])}</b><span>${esc(row[0])} · ${esc(row[2])}</span></div>
      </div>`).join('');

    const outboxList = outbox.outbox || [];
    const userList = users.users || [];

    body.innerHTML = `
    <div class="panel">
      <h3>Статистика сервера</h3>
      <div class="desc">Сводные показатели OrangeM.</div>
      <div class="card-grid">${statCards}</div>
    </div>
    <div class="panel">
      <h3>Исходящие коды (демо-режим доставки)</h3>
      <div class="desc">Письма и SMS не отправляются по-настоящему — коды складываются в этот журнал.</div>
      <div id="ad-outbox">${outboxList.length ? outboxList.map(outboxRow).join('') : '<div class="hint">Журнал пуст.</div>'}</div>
    </div>
    <div class="panel">
      <h3>Пользователи</h3>
      <div class="desc">Поиск по логину, имени, почте или телефону. Блокировка завершает все сессии пользователя.</div>
      <div class="form-row">
        <input class="input" id="ad-q" placeholder="Логин, имя, почта или телефон">
        <button class="btn" data-pact="ad-search">Найти</button>
      </div>
      <div id="ad-users" style="margin-top:12px">${userList.length ? userList.map(adminUserRow).join('') : '<div class="hint">Пользователей не найдено.</div>'}</div>
    </div>
    <div class="panel">
      <h3>Рассылка</h3>
      <div class="desc">Сообщение придёт всем подключённым пользователям в реальном времени.</div>
      <div class="field"><label>Текст рассылки</label>
        <textarea id="ad-text" maxlength="500" placeholder="Текст уведомления всем пользователям"></textarea></div>
      <button class="btn primary" data-pact="ad-broadcast">Отправить всем</button>
      <div class="okmsg hidden" id="ad-ok">Рассылка отправлена</div>
      <div class="err hidden" id="ad-err"></div>
    </div>`;

    const search = OM.debounce(runAdminSearch, 400);
    $('ad-q').addEventListener('input', search);
    $('ad-q').addEventListener('keydown', e => { if (e.key === 'Enter') runAdminSearch(); });
    body.querySelector('[data-pact="ad-search"]').onclick = runAdminSearch;
    body.querySelector('[data-pact="ad-broadcast"]').onclick = adminBroadcast;
    bindAdminUserActions();
  }

  function outboxRow(m) {
    return `<div class="member-row">
      <div class="cw" style="flex:1;min-width:0">
        <div class="comm-name" style="font-size:14px">${esc(m.channel || '—')} → ${esc(m.target || '—')}</div>
        ${m.subject ? `<div class="hint">${esc(m.subject)}</div>` : ''}
        <div class="post-body" style="font-size:13px">${esc(m.body || '')}</div>
      </div>
      <span class="hint">${esc(OM.fmtDateTime(m.created_at))}</span>
    </div>`;
  }

  function adminUserRow(u) {
    return `<div class="member-row">
      ${OM.avatar(u, { size: 'sm' })}
      <div class="cw" style="flex:1;min-width:0">
        <div class="comm-name" style="font-size:14px">${esc(u.display_name || u.username)}
          ${u.is_admin ? '<span class="badge orange">админ</span>' : ''}
          ${u.banned ? '<span class="badge grey">заблокирован</span>' : ''}</div>
        <div class="hint">@${esc(u.username)} · ${esc(u.orange_id || '')}</div>
        <div class="hint">${esc(u.email || 'без почты')} · ${esc(u.phone || 'без телефона')}</div>
      </div>
      <button class="btn small ${u.banned ? 'ghost' : 'danger'}" data-ban="${u.id}" data-value="${u.banned ? '0' : '1'}">
        ${u.banned ? 'Разблокировать' : 'Заблокировать'}
      </button>
    </div>`;
  }

  function bindAdminUserActions() {
    const box = $('ad-users');
    if (!box) return;
    box.onclick = async e => {
      const b = e.target.closest('[data-ban]');
      if (!b) return;
      const ban = b.dataset.value === '1';
      b.disabled = true;
      try {
        await OM.api.post('/api/admin/users/' + b.dataset.ban + '/ban', { ban });
        OM.toast(ban ? 'Пользователь заблокирован' : 'Пользователь разблокирован', 'ok');
        runAdminSearch();
      } catch (err) {
        b.disabled = false;
        OM.toast(err.message || 'Не удалось изменить блокировку', 'err');
      }
    };
  }

  async function runAdminSearch() {
    const box = $('ad-users');
    const q = $('ad-q');
    if (!box || !q) return;
    box.innerHTML = '<div class="hint">Поиск…</div>';
    try {
      const r = await OM.api.get('/api/admin/users?q=' + encodeURIComponent(q.value.trim()));
      const list = r.users || [];
      box.innerHTML = list.length ? list.map(adminUserRow).join('') : '<div class="hint">Никого не найдено.</div>';
    } catch (e) {
      box.innerHTML = `<div class="err">${esc(e.message || 'Ошибка поиска')}</div>`;
    }
  }

  async function adminBroadcast() {
    const ok = $('ad-ok');
    const err = $('ad-err');
    const text = ($('ad-text').value || '').trim();
    ok.classList.add('hidden');
    err.classList.add('hidden');
    if (!text) { err.textContent = 'Введите текст рассылки'; err.classList.remove('hidden'); return; }
    try {
      await OM.api.post('/api/admin/broadcast', { text });
      $('ad-text').value = '';
      ok.classList.remove('hidden');
      OM.toast('Рассылка отправлена', 'ok');
    } catch (e) {
      err.textContent = e.message || 'Не удалось отправить рассылку';
      err.classList.remove('hidden');
    }
  }

  /* ======================================================================= разовые делегирования */
  // Разблокировка пользователей (список в разделе «Конфиденциальность»)
  document.addEventListener('click', e => {
    const b = e.target.closest('[data-unblock]');
    if (b) unblockUser(b.dataset.unblock);
  });

  /* ======================================================================= регистрация раздела */
  OM.views.settings = {
    enter(params) {
      bindNav();
      if (params && params.tab && TABS.indexOf(params.tab) >= 0) setTab(params.tab);
      else {
        const active = document.querySelector('#settings-nav .chip.active');
        if (active && TABS.indexOf(active.dataset.tab) >= 0) current = active.dataset.tab;
      }
      applyAppearance();
      render();
    },
    leave() {}
  };

  // Применяем сохранённую тему сразу, если профиль уже загружен
  applyAppearance();
  OM.on('me-updated', applyAppearance);
})();
