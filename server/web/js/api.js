/* OrangeM — HTTP client + глобальное состояние */
window.OM = window.OM || {};

OM.api = (function () {
  const TOKEN_KEY = 'orangem_token';
  let token = localStorage.getItem(TOKEN_KEY) || '';

  function setToken(t) {
    token = t || '';
    if (t) localStorage.setItem(TOKEN_KEY, t);
    else localStorage.removeItem(TOKEN_KEY);
  }

  function headers(extra) {
    const h = Object.assign({ 'Accept': 'application/json' }, extra || {});
    if (token) h['X-Orange-Token'] = token;
    return h;
  }

  async function request(method, path, body, opts) {
    opts = opts || {};
    const init = { method, headers: headers(opts.headers) };
    if (body !== undefined && body !== null) {
      if (body instanceof FormData || body instanceof Blob || typeof body === 'string') {
        init.body = body;
      } else {
        init.headers['Content-Type'] = 'application/json';
        init.body = JSON.stringify(body);
      }
    }
    let res;
    try {
      res = await fetch(path, init);
    } catch (e) {
      throw { status: 0, message: 'Нет связи с сервером OrangeM' };
    }
    let data = null;
    const ct = res.headers.get('content-type') || '';
    if (ct.indexOf('application/json') >= 0) {
      try { data = await res.json(); } catch (e) { data = null; }
    } else if (opts.rawText) {
      data = { text: await res.text() };
    }
    if (!res.ok) {
      const msg = (data && (data.error || data.message)) || ('Ошибка ' + res.status);
      if (res.status === 401 && OM.onUnauthorized) OM.onUnauthorized();
      throw { status: res.status, message: msg, data };
    }
    return data || {};
  }

  return {
    get: (p, o) => request('GET', p, null, o),
    post: (p, b, o) => request('POST', p, b, o),
    patch: (p, b, o) => request('PATCH', p, b, o),
    del: (p, b, o) => request('DELETE', p, b, o),
    request,
    setToken,
    getToken: () => token,
    hasToken: () => !!token,

    /** Загрузка файла на сервер. Возвращает {url, kind, size} */
    async upload(file, kind) {
      const q = '/api/upload?name=' + encodeURIComponent(file.name || 'file') +
                (kind ? '&kind=' + encodeURIComponent(kind) : '');
      return request('POST', q, file, { headers: { 'Content-Type': file.type || 'application/octet-stream' } });
    }
  };
})();

/* Глобальное состояние приложения */
OM.state = {
  me: null,          // текущий пользователь (полный профиль)
  chats: [],
  activeChat: null,
  onlineUsers: {},   // id -> true
  unreadTotal: 0,
  feedScope: 'global',
  feedQuery: '',
  feedTag: '',
  notifications: []
};

OM.on = function (evt, fn) {
  OM._ev = OM._ev || {};
  (OM._ev[evt] = OM._ev[evt] || []).push(fn);
};
OM.emit = function (evt, data) {
  const list = (OM._ev && OM._ev[evt]) || [];
  for (const fn of list) {
    try { fn(data); } catch (e) { console.error('handler error', evt, e); }
  }
};
