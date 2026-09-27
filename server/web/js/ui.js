/* OrangeM — UI-утилиты: тосты, модалки, аватары, время, форматирование */
window.OM = window.OM || {};

(function () {
  const ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

  OM.esc = function (s) {
    return String(s === undefined || s === null ? '' : s).replace(/[&<>"']/g, c => ESC[c]);
  };

  OM.initials = function (u) {
    if (!u) return '?';
    const name = u.display_name || u.username || u.name || '?';
    const parts = String(name).trim().split(/\s+/);
    if (parts.length >= 2) return (parts[0][0] + parts[1][0]).toUpperCase();
    return String(name).slice(0, 2).toUpperCase();
  };

  OM.avatarColor = function (u) {
    const seed = String((u && (u.id || u.orange_id || u.username)) || 'x');
    let h = 0;
    for (let i = 0; i < seed.length; i++) h = (h * 31 + seed.charCodeAt(i)) % 360;
    return `linear-gradient(135deg, hsl(${h} 70% 45%), hsl(${(h + 40) % 360} 70% 35%))`;
  };

  /**
   * HTML-аватар.
   * OM.avatar(user, {size:'sm'|'md'|'lg'|'xl'|'xs', ring:bool, seen:bool, online:bool})
   */
  OM.avatar = function (u, o) {
    o = o || {};
    const cls = 'avatar' + (o.size && o.size !== 'md' ? ' ' + o.size : '');
    const url = u && u.avatar ? u.avatar : '';
    const inner = url
      ? `<img class="${cls}" src="${OM.esc(url)}" alt="">`
      : `<div class="${cls}" style="background:${OM.avatarColor(u)}">${OM.esc(OM.initials(u))}</div>`;
    let html = inner;
    if (o.online !== undefined) {
      html = `<span class="avatar-wrap">${inner}<i class="presence-dot ${o.online ? 'online' : ''}"></i></span>`;
    }
    if (o.ring) html = `<span class="avatar-ring ${o.seen ? 'seen' : ''}">${html}</span>`;
    return html;
  };

  OM.fmtTime = function (ts) {
    if (!ts) return '';
    const d = new Date(ts * 1000);
    return d.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' });
  };

  OM.fmtDateTime = function (ts) {
    if (!ts) return '';
    const d = new Date(ts * 1000);
    return d.toLocaleString('ru-RU', { day: '2-digit', month: '2-digit', year: '2-digit', hour: '2-digit', minute: '2-digit' });
  };

  OM.dayLabel = function (ts) {
    const d = new Date(ts * 1000), now = new Date();
    const same = (a, b) => a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    const y = new Date(now.getTime() - 86400000);
    if (same(d, now)) return 'Сегодня';
    if (same(d, y)) return 'Вчера';
    return d.toLocaleDateString('ru-RU', { day: '2-digit', month: 'long' });
  };

  OM.timeAgo = function (ts) {
    if (!ts) return '';
    const s = Math.max(0, Math.floor(Date.now() / 1000) - ts);
    if (s < 60) return 'только что';
    const m = Math.floor(s / 60);
    if (m < 60) return m + ' мин назад';
    const h = Math.floor(m / 60);
    if (h < 24) return h + ' ч назад';
    const d = Math.floor(h / 24);
    if (d < 7) return d + ' дн назад';
    return OM.fmtDateTime(ts);
  };

  OM.onlineText = function (u) {
    if (!u) return '';
    if (u.presence && u.presence.online) return 'в сети';
    const p = u.presence || {};
    if (p.last_seen_text) return 'был(а) ' + OM.timeAgo(Number(p.last_seen) || 0);
    if (p.hidden) return 'недавно';
    return 'не в сети';
  };

  /** Экранирует текст, ссылки/хештеги/упоминания делает кликабельными */
  OM.linkify = function (text) {
    let s = OM.esc(text || '');
    s = s.replace(/(https?:\/\/[^\s<]+)/g, m => `<a href="${m}" target="_blank" rel="noopener">${m}</a>`);
    s = s.replace(/(^|\s)(#[\wа-яёА-ЯЁ_]{2,})/g, (m, a, b) => `${a}<a href="#" class="om-tag" data-tag="${b.slice(1)}">${b}</a>`);
    s = s.replace(/(^|\s)(@[\w._-]{3,})/g, (m, a, b) => `${a}<a href="#" class="om-mention" data-user="${b.slice(1)}">${b}</a>`);
    return s;
  };

  OM.fmtNum = function (n) {
    n = Number(n) || 0;
    if (n >= 1000000) return (n / 1000000).toFixed(1).replace('.0', '') + 'M';
    if (n >= 1000) return (n / 1000).toFixed(1).replace('.0', '') + 'K';
    return String(n);
  };

  /* ---------------- toast ---------------- */
  OM.toast = function (msg, kind, ms) {
    const root = document.getElementById('toast-root');
    const el = document.createElement('div');
    el.className = 'toast' + (kind ? ' ' + kind : '');
    el.textContent = msg;
    root.appendChild(el);
    setTimeout(() => { el.style.opacity = '0'; el.style.transform = 'translateX(20px)'; el.style.transition = '.25s'; }, (ms || 3600) - 250);
    setTimeout(() => el.remove(), ms || 3600);
  };

  /* ---------------- modal ---------------- */
  OM.modal = function (opts) {
    const root = document.getElementById('modal-root');
    const wrap = document.createElement('div');
    wrap.className = 'modal';
    const bodyHtml = typeof opts.body === 'string' ? opts.body : '';
    wrap.innerHTML = `
      <div class="modal-card" style="${opts.width ? 'width:' + opts.width + 'px' : ''}">
        <div class="modal-head"><h3>${OM.esc(opts.title || '')}</h3><button class="x" data-close>${OM.icon('close', 16)}</button></div>
        <div class="modal-body">${bodyHtml}</div>
        ${opts.footer === false ? '' : '<div class="modal-foot"></div>'}
      </div>`;
    const card = wrap.querySelector('.modal-card');
    const bodyEl = wrap.querySelector('.modal-body');
    if (typeof opts.body !== 'string' && opts.body instanceof Node) bodyEl.appendChild(opts.body);
    const foot = wrap.querySelector('.modal-foot');
    const api = {
      root: wrap, body: bodyEl, card,
      close() { wrap.remove(); if (opts.onClose) opts.onClose(); }
    };
    if (foot && opts.actions) {
      opts.actions.forEach(a => {
        const b = document.createElement('button');
        b.className = 'btn ' + (a.cls || 'ghost');
        b.textContent = a.label;
        b.onclick = () => a.onClick ? a.onClick(api) : api.close();
        foot.appendChild(b);
      });
    }
    wrap.addEventListener('click', e => { if (e.target === wrap) api.close(); });
    wrap.querySelector('[data-close]').onclick = api.close;
    root.appendChild(wrap);
    return api;
  };

  OM.confirm = function (text, onYes, yesLabel) {
    OM.modal({
      title: 'Подтверждение',
      body: `<p style="line-height:1.6">${OM.esc(text)}</p>`,
      actions: [
        { label: 'Отмена', cls: 'ghost' },
        { label: yesLabel || 'Да', cls: 'primary', onClick: m => { m.close(); onYes(); } }
      ]
    });
  };

  OM.prompt = function (opts, onOk) {
    const m = OM.modal({
      title: opts.title || 'Ввод',
      body: `<div class="field"><label>${OM.esc(opts.label || '')}</label>
        <input class="input" id="om-prompt-input" type="${opts.type || 'text'}"
          placeholder="${OM.esc(opts.placeholder || '')}" value="${OM.esc(opts.value || '')}"></div>
        ${opts.hint ? `<div class="hint">${OM.esc(opts.hint)}</div>` : ''}<div class="err hidden"></div>`,
      actions: [
        { label: 'Отмена', cls: 'ghost' },
        { label: opts.ok || 'ОК', cls: 'primary', onClick: async m2 => {
            const v = m2.body.querySelector('#om-prompt-input').value;
            const err = m2.body.querySelector('.err');
            try { await onOk(v, m2); } catch (e) { err.textContent = e.message || 'Ошибка'; err.classList.remove('hidden'); }
          } }
      ]
    });
    setTimeout(() => { const i = m.body.querySelector('#om-prompt-input'); if (i) i.focus(); }, 60);
    return m;
  };

  OM.copy = async function (text) {
    try {
      await navigator.clipboard.writeText(text);
      OM.toast('Скопировано', 'ok');
    } catch (e) {
      OM.toast('Не удалось скопировать', 'err');
    }
  };

  OM.debounce = function (fn, ms) {
    let t;
    return function (...a) { clearTimeout(t); t = setTimeout(() => fn.apply(this, a), ms || 300); };
  };

  OM.byteSize = function (n) {
    n = Number(n) || 0;
    if (n < 1024) return n + ' Б';
    if (n < 1048576) return (n / 1024).toFixed(1) + ' КБ';
    return (n / 1048576).toFixed(1) + ' МБ';
  };

  OM.el = function (html) {
    const d = document.createElement('div');
    d.innerHTML = html.trim();
    return d.firstElementChild;
  };

  /* ---------------- иконки (SVG, без эмодзи) ----------------
   * OM.icon('search', 18) -> строка с <svg>. Набор согласован со style.css (.ico).
   * Все иконки рисуются линией 1.6, цвет наследуется от текста (currentColor). */
  const ICON_PATHS = {
    home: '<path d="M3 10.6 12 3l9 7.6"/><path d="M5.5 9.4V21h13V9.4"/><path d="M9.8 21v-6.2h4.4V21"/>',
    chat: '<path d="M21 11.6c0 4.2-4 7.6-9 7.6-1 0-2-.14-2.9-.4L4 21l1.3-3.6C4.1 16.2 3 14 3 11.6 3 7.4 7 4 12 4s9 3.4 9 7.6z"/>',
    users: '<path d="M16.5 20v-1.6a3.9 3.9 0 0 0-3.9-3.9H7.4a3.9 3.9 0 0 0-3.9 3.9V20"/><circle cx="10" cy="7.6" r="3.6"/><path d="M20.5 20v-1.6a3.9 3.9 0 0 0-2.9-3.8"/><path d="M15.5 4.2a3.6 3.6 0 0 1 0 7"/>',
    stories: '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="3.2"/>',
    settings: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1A1.7 1.7 0 0 0 9 19.4a1.7 1.7 0 0 0-1.9.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.9 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1A1.7 1.7 0 0 0 4.6 9a1.7 1.7 0 0 0-.3-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1A1.7 1.7 0 0 0 9 4.6h.1A1.7 1.7 0 0 0 10 3.1V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9v.1a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    search: '<circle cx="10.8" cy="10.8" r="7.2"/><path d="M16.2 16.2 21 21"/>',
    send: '<path d="M21.5 2.5 2.8 10.2l7.2 2.9 2.9 7.2z"/><path d="M21.5 2.5 10 12.9"/>',
    attach: '<path d="M20.4 11.6 12 20a5.2 5.2 0 0 1-7.4-7.4l8.7-8.7a3.5 3.5 0 0 1 4.9 4.9l-8.7 8.7a1.7 1.7 0 0 1-2.5-2.5l8-8"/>',
    image: '<rect x="3" y="4.5" width="18" height="15" rx="2"/><circle cx="8.6" cy="9.6" r="1.6"/><path d="M3.5 16.8 9 12l3.5 3 3-2.5 5 4.5"/>',
    video: '<rect x="2.5" y="5.5" width="13" height="13" rx="2"/><path d="M15.5 10.5 21.5 7v10l-6-3.5z"/>',
    music: '<path d="M9 18V6.5l10-2v11"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="16.5" cy="15.5" r="2.5"/>',
    file: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>',
    heart: '<path d="M12 20.3 4.9 13.2a4.6 4.6 0 1 1 6.5-6.5l.6.6.6-.6a4.6 4.6 0 1 1 6.5 6.5z"/>',
    comment: '<path d="M21 11.5c0 4-4 7.2-9 7.2-1 0-2-.13-2.9-.37L4 21l1.3-3.5C4.1 16 3 13.9 3 11.5 3 7.6 7 4.3 12 4.3s9 3.3 9 7.2z"/><path d="M8.5 10.5h7M8.5 13.5h4"/>',
    link: '<path d="M10 13.5a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1.2 1.2"/><path d="M14 10.5a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1.2-1.2"/>',
    trash: '<path d="M4 7h16M9.5 7V4.8h5V7M6.5 7l1 13.2h9L17.5 7"/><path d="M10.5 11v5.5M13.5 11v5.5"/>',
    edit: '<path d="M4 20h4l10-10a2.8 2.8 0 0 0-4-4L4 16z"/><path d="M14.5 6.5 17.5 9.5"/>',
    reply: '<path d="M9 8H6l-3 3.5L6 15h3"/><path d="M6 11.5h7.5a5 5 0 0 1 5 5V19"/>',
    react: '<circle cx="12" cy="12" r="8.6"/><path d="M9 14.5a3.8 3.8 0 0 0 6 0"/><path d="M9.2 9.6h.01M14.8 9.6h.01"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    close: '<path d="M6 6l12 12M18 6 6 18"/>',
    'arrow-left': '<path d="M20 12H4M10 6 4 12l6 6"/>',
    'arrow-right': '<path d="M4 12h16M14 6l6 6-6 6"/>',
    'arrow-up': '<path d="M12 20V4M6 10l6-6 6 6"/>',
    'chevron-left': '<path d="M15 5 8 12l7 7"/>',
    'chevron-right': '<path d="M9 5l7 7-7 7"/>',
    'chevron-down': '<path d="M5 9l7 7 7-7"/>',
    pin: '<path d="M15.5 3.5 20.5 8.5l-3 1.2-3.6 3.6-.6 3-2.6-2.6-4 4-1.3-1.3 4-4-2.6-2.6 3-.6 3.6-3.6z"/>',
    mute: '<path d="M4 9.5h3l4-3.5v12l-4-3.5H4z"/><path d="M15 9.5 20 15M20 9.5 15 15"/>',
    eye: '<path d="M2.5 12S6 6.5 12 6.5 21.5 12 21.5 12 18 17.5 12 17.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="2.8"/>',
    lock: '<rect x="4.8" y="10.5" width="14.4" height="10" rx="2"/><path d="M8.2 10.5V8a3.8 3.8 0 0 1 7.6 0v2.5"/>',
    mail: '<rect x="3" y="5.5" width="18" height="13" rx="2"/><path d="m3.6 7 8.4 6 8.4-6"/>',
    phone: '<path d="M7.5 3.5h3l1.5 4-2 1.5a11 11 0 0 0 5 5l1.5-2 4 1.5v3a2 2 0 0 1-2.2 2A16.5 16.5 0 0 1 5.5 5.7 2 2 0 0 1 7.5 3.5z"/>',
    key: '<circle cx="8" cy="14" r="3.5"/><path d="M10.5 11.5 19 3M16 6l2.5 2.5M14 8l2.5 2.5"/>',
    qr: '<rect x="3.5" y="3.5" width="6.5" height="6.5" rx="1"/><rect x="14" y="3.5" width="6.5" height="6.5" rx="1"/><rect x="3.5" y="14" width="6.5" height="6.5" rx="1"/><path d="M14 14h3v3h-3zM20.5 14v3M17.5 20.5h3M14 20.5h.01"/>',
    shield: '<path d="M12 3 5 5.8v5.4c0 4.3 3 7.6 7 9.8 4-2.2 7-5.5 7-9.8V5.8z"/>',
    copy: '<rect x="9" y="9" width="11.5" height="11.5" rx="2"/><path d="M15.5 6.2V5A2 2 0 0 0 13.5 3H5a2 2 0 0 0-2 2v8.5a2 2 0 0 0 2 2h1.2"/>',
    check: '<path d="M4.5 12.5 9.5 17.5 19.5 6.5"/>',
    refresh: '<path d="M20 11.5A8 8 0 0 1 6.3 17.7L4 15.5"/><path d="M4 12.5A8 8 0 0 1 17.7 6.3L20 8.5"/><path d="M20 3.5v5h-5M4 20.5v-5h5"/>',
    logout: '<path d="M15 4.5h3.5a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2H15"/><path d="M11 8 7 12l4 4M7 12h9"/>',
    user: '<circle cx="12" cy="8" r="3.8"/><path d="M4.5 20.5v-1.2A5.3 5.3 0 0 1 9.8 14h4.4a5.3 5.3 0 0 1 5.3 5.3v1.2"/>',
    bell: '<path d="M18 15.5V11a6 6 0 1 0-12 0v4.5L4.5 18h15z"/><path d="M9.8 21h4.4"/>',
    chart: '<path d="M4 20.5h16"/><path d="M7 20.5V12M12 20.5V5.5M17 20.5v-5"/>',
    globe: '<circle cx="12" cy="12" r="8.6"/><path d="M3.4 12h17.2M12 3.4c2.4 2.6 3.6 5.4 3.6 8.6S14.4 18 12 20.6c-2.4-2.6-3.6-5.4-3.6-8.6S9.6 6 12 3.4z"/>',
    dots: '<circle cx="5.5" cy="12" r="1.5"/><circle cx="12" cy="12" r="1.5"/><circle cx="18.5" cy="12" r="1.5"/>',
    crown: '<path d="M3.5 18.5h17L19 7.5l-4.5 4L12 5.5 9.5 11.5 5 7.5z"/>',
    star: '<path d="m12 4 2.5 5.2 5.5.8-4 3.9 1 5.5-5-2.7-5 2.7 1-5.5-4-3.9 5.5-.8z"/>',
    info: '<circle cx="12" cy="12" r="8.6"/><path d="M12 11v5.5M12 7.9h.01"/>',
    warning: '<path d="M12 4.5 21 19.5H3z"/><path d="M12 10v4M12 16.6h.01"/>',
    clock: '<circle cx="12" cy="12" r="8.6"/><path d="M12 7.5V12l3 2"/>',
    camera: '<path d="M4 7.5h3l1.5-2.5h7L17 7.5h3A1.5 1.5 0 0 1 21.5 9v9A1.5 1.5 0 0 1 20 19.5H4A1.5 1.5 0 0 1 2.5 18V9A1.5 1.5 0 0 1 4 7.5z"/><circle cx="12" cy="13" r="3.4"/>',
    film: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M7.5 5v14M16.5 5v14M3 12h18"/>',
    tag: '<path d="M11 3.5H5.5A2 2 0 0 0 3.5 5.5V11l9.5 9.5 8-8z"/><circle cx="7.8" cy="7.8" r="1.3"/>',
    ban: '<circle cx="12" cy="12" r="8.6"/><path d="M6.2 17.8 17.8 6.2"/>',
    filter: '<path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/>',
    verify: '<path d="m12 3.5 2.2 1.6 2.7-.2.9 2.6 2.2 1.6-1 2.5 1 2.5-2.2 1.6-.9 2.6-2.7-.2L12 20.5l-2.2-1.6-2.7.2-.9-2.6L4 14.9l1-2.5-1-2.5 2.2-1.6.9-2.6 2.7.2z"/><path d="m9.2 12 2 2 4-4"/>',
    logout_all: '<path d="M9 4.5H5.5a2 2 0 0 0-2 2v11a2 2 0 0 0 2 2H9"/><path d="M15 8.5 18.5 12 15 15.5M8 12h10"/>',
    download: '<path d="M12 4v11M7.5 11 12 15.5 16.5 11"/><path d="M4.5 20h15"/>',
    upload: '<path d="M12 20V9M7.5 13 12 8.5 16.5 13"/><path d="M4.5 4h15"/>',
    grid: '<rect x="3.5" y="3.5" width="7" height="7" rx="1.4"/><rect x="13.5" y="3.5" width="7" height="7" rx="1.4"/><rect x="3.5" y="13.5" width="7" height="7" rx="1.4"/><rect x="13.5" y="13.5" width="7" height="7" rx="1.4"/>',
    megaphone: '<path d="M4 10.5v3l3 .8V9.7z"/><path d="M7 9.7 19 5v14L7 14.3z"/><path d="M9.5 15v4.5h3V16"/>'
  };

  /**
   * SVG-иконка интерфейса.
   * @param {string} name ключ из набора (home, chat, search, send, ...)
   * @param {object|number} [opts] размер или {size, cls, stroke}
   */
  OM.icon = function (name, opts) {
    if (typeof opts === 'number') opts = { size: opts };
    opts = opts || {};
    const size = opts.size || 18;
    const body = ICON_PATHS[name] || ICON_PATHS.info;
    const cls = 'ico' + (opts.cls ? ' ' + opts.cls : '');
    return '<svg class="' + cls + '" width="' + size + '" height="' + size +
      '" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="' + (opts.stroke || 1.6) +
      '" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + body + '</svg>';
  };
  OM.iconNames = Object.keys(ICON_PATHS);


  /** Скачивание файла на устройство */
  OM.download = function (name, content, mime) {
    const blob = new Blob([content], { type: mime || 'application/octet-stream' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  };
})();
