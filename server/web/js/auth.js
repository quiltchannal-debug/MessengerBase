/* OrangeM — экран входа и регистрации: #view-auth → #auth-body
   Раздел регистрируется как OM.views.auth.
   4 способа входа: Почта, Телефон (пароль или одноразовый код), Флеш-ключ, Аутентификатор.
   Плюс регистрация по почте/телефону с кодом подтверждения и восстановление пароля. */
window.OM = window.OM || {};

(function () {
  'use strict';

  /* ------------------------------------------------------------------ состояние экрана */
  let screen = 'login';        // 'login' | 'register' | 'reset'
  let loginMethod = 'email';   // 'email' | 'phone' | 'flash' | 'totp'
  let regMethod = 'email';     // 'email' | 'phone'
  let phoneMode = 'password';  // вход по телефону: 'password' | 'code'
  let otpStage = 'request';    // 'request' | 'verify'
  let otpIdent = '';           // телефон, на который запрошен код
  let otpDev = '';             // dev_code из ответа otp/request
  let totpChallenge = '';      // challenge из ответа {need_totp:true}
  let regStage = 'form';       // 'form' | 'verify'
  let regPending = null;       // {target, channel, dev_code}
  let resetStage = 'request';  // 'request' | 'verify'
  let resetIdent = '';
  let resetDev = '';

  /* ------------------------------------------------------------------ мелкие утилиты */
  function authBody() { return document.getElementById('auth-body'); }

  /** Значение поля формы по имени (без опоры на named-getter формы). */
  function fv(form, name) {
    const el = form && form.elements ? form.elements.namedItem(name) : null;
    return el && typeof el.value === 'string' ? el.value : '';
  }

  /** Фокус на поле формы по имени. */
  function ff(form, name) {
    const el = form && form.elements ? form.elements.namedItem(name) : null;
    if (el && el.focus) el.focus();
    return el;
  }

  /** Блок с демо-кодом доставки: всегда показываем + кнопка «вставить». */
  function devBlock(code) {
    if (!code) return '';
    return '<div class="hint">Код (демо-режим доставки): <b>' + OM.esc(code) + '</b> ' +
      '<button type="button" class="btn small ghost" data-fill="' + OM.esc(code) + '">Вставить код</button></div>';
  }

  /** Общая обвязка формы: блокировка кнопки, inline .err + тост. */
  function formSubmit(form, btn, handler) {
    form.addEventListener('submit', async function (ev) {
      ev.preventDefault();
      if (form._busy) return;
      const err = form.querySelector('.err');
      if (err) { err.textContent = ''; err.classList.add('hidden'); }
      form._busy = true;
      if (btn) btn.disabled = true;
      try {
        await handler();
      } catch (e) {
        const msg = (e && e.message) || 'Не удалось выполнить запрос';
        if (err) { err.textContent = msg; err.classList.remove('hidden'); }
        OM.toast(msg, 'err');
      } finally {
        form._busy = false;
        if (btn) btn.disabled = false;
      }
    });
  }

  /** Успешный вход: токен → профиль → websocket → приложение. */
  function finish(res) {
    if (!res || !res.token || !res.user) throw { message: 'Сервер не вернул сессию' };
    OM.api.setToken(res.token);
    OM.setMe(res.user);
    OM.ws.connect();
    OM.showApp();
    OM.toast('Добро пожаловать, ' + (res.user.display_name || res.user.username) + '!', 'ok');
  }

  /* ------------------------------------------------------------------ разметка: вход */
  const LOGIN_METHODS = [
    { key: 'email', ico: 'mail', label: 'Почта' },
    { key: 'phone', ico: 'phone', label: 'Телефон' },
    { key: 'flash', ico: 'key', label: 'Флеш-ключ' },
    { key: 'totp', ico: 'qr', label: 'Аутентификатор' }
  ];

  function methodGrid() {
    let html = '<div class="method-grid">';
    LOGIN_METHODS.forEach(function (m) {
      html += '<button type="button" class="method-btn' + (loginMethod === m.key ? ' active' : '') +
        '" data-loginmethod="' + m.key + '">' + OM.icon(m.ico, 18) + m.label + '</button>';
    });
    return html + '</div>';
  }

  function emailPanel() {
    return '<form id="auth-form-login" novalidate>' +
      '<div class="field"><label>Почта, логин или Orange ID</label>' +
        '<input class="input" name="identifier" type="text" autocomplete="username" placeholder="you@example.com"></div>' +
      '<div class="field"><label>Пароль</label>' +
        '<input class="input" name="password" type="password" autocomplete="current-password" placeholder="••••••••"></div>' +
      '<button class="btn primary wide" type="submit">Войти</button>' +
      '<div class="err hidden"></div>' +
      '<div class="hint">Забыли пароль? <a href="#" data-act="reset">Восстановить доступ</a></div>' +
      '</form>';
  }

  function phonePanel() {
    if (phoneMode === 'password') {
      return '<form id="auth-form-phone" novalidate>' +
        '<div class="head-chips" style="margin-bottom:12px">' +
          '<button type="button" class="chip active" data-phone-mode="password">Пароль</button>' +
          '<button type="button" class="chip" data-phone-mode="code">Одноразовый код</button>' +
        '</div>' +
        '<div class="field"><label>Номер телефона</label>' +
          '<input class="input" name="identifier" type="tel" autocomplete="tel" placeholder="+7 900 000-00-00"></div>' +
        '<div class="field"><label>Пароль</label>' +
          '<input class="input" name="password" type="password" autocomplete="current-password" placeholder="••••••••"></div>' +
        '<button class="btn primary wide" type="submit">Войти</button>' +
        '<div class="err hidden"></div>' +
        '<div class="hint">Забыли пароль? <a href="#" data-act="reset">Восстановить доступ</a></div>' +
        '</form>';
    }
    if (otpStage === 'request') {
      return '<form id="auth-form-otp-request" novalidate>' +
        '<div class="head-chips" style="margin-bottom:12px">' +
          '<button type="button" class="chip" data-phone-mode="password">Пароль</button>' +
          '<button type="button" class="chip active" data-phone-mode="code">Одноразовый код</button>' +
        '</div>' +
        '<div class="field"><label>Номер телефона</label>' +
          '<input class="input" name="identifier" type="tel" autocomplete="tel" placeholder="+7 900 000-00-00"></div>' +
        '<button class="btn primary wide" type="submit">Получить код</button>' +
        '<div class="err hidden"></div>' +
        '<div class="hint">Пришлём 6-значный код по SMS, если аккаунт с таким номером существует.</div>' +
        '</form>';
    }
    return '<form id="auth-form-otp-verify" novalidate>' +
      '<div class="hint">Код отправлен на <b>' + OM.esc(otpIdent) + '</b>.</div>' +
      devBlock(otpDev) +
      '<div class="field"><label>Одноразовый код</label>' +
        '<input class="input" name="code" data-code-input inputmode="numeric" maxlength="6" autocomplete="one-time-code" placeholder="000000"></div>' +
      '<button class="btn primary wide" type="submit">Войти</button>' +
      '<div class="err hidden"></div>' +
      '<div class="hint"><a href="#" data-act="otp-restart">' + OM.icon('arrow-left', 14) + ' Изменить номер</a></div>' +
      '</form>';
  }

  function flashPanel() {
    const sup = !!(OM.flashKey && OM.flashKey.supported);
    return '<form id="auth-form-flash" novalidate>' +
      '<div class="hint">' + (sup
        ? 'Выберите файл ключа .omkey на носителе — сервер будет перешифровывать его в реальном времени.'
        : 'Браузер не даёт прямой доступ к файлам: ключ будет скачан, а после входа обновлён.') + '</div>' +
      '<div class="field"><label>Название устройства</label>' +
        '<input class="input" name="device" type="text" value="Флешка-ключ"></div>' +
      '<button class="btn primary wide" type="submit">Выбрать файл ключа</button>' +
      '<div class="err hidden"></div>' +
      '<div class="hint">Ключ создаётся в настройках → «Устройства и флеш-ключи».</div>' +
      '</form>';
  }

  function totpPanel() {
    if (!totpChallenge) {
      // Первый шаг аутентификатора — обычный вход по паролю.
      return '<form id="auth-form-totp-start" novalidate>' +
        '<div class="hint">Сначала пароль, затем 6-значный код из приложения-аутентификатора.</div>' +
        '<div class="field"><label>Логин, почта или Orange ID</label>' +
          '<input class="input" name="identifier" type="text" autocomplete="username" placeholder="you@example.com"></div>' +
        '<div class="field"><label>Пароль</label>' +
          '<input class="input" name="password" type="password" autocomplete="current-password" placeholder="••••••••"></div>' +
        '<button class="btn primary wide" type="submit">Продолжить</button>' +
        '<div class="err hidden"></div>' +
        '</form>';
    }
    return '<form id="auth-form-totp" novalidate>' +
      '<div class="hint">Пароль принят. Введите код из приложения-аутентификатора.</div>' +
      '<div class="field"><label>Одноразовый код</label>' +
        '<input class="input" name="code" data-code-input inputmode="numeric" maxlength="6" autocomplete="one-time-code" placeholder="000000"></div>' +
      '<button class="btn primary wide" type="submit">Подтвердить</button>' +
      '<div class="err hidden"></div>' +
      '<div class="hint"><a href="#" data-act="login-back">' + OM.icon('arrow-left', 14) + ' Назад ко входу</a></div>' +
      '</form>';
  }

  function loginPanel() {
    let inner;
    if (loginMethod === 'phone') inner = phonePanel();
    else if (loginMethod === 'flash') inner = flashPanel();
    else if (loginMethod === 'totp') inner = totpPanel();
    else inner = emailPanel();
    return methodGrid() + inner;
  }

  /* ------------------------------------------------------------------ разметка: регистрация */
  function registerPanel() {
    if (regStage === 'verify') {
      const ch = regPending && regPending.channel === 'phone' ? 'SMS' : 'письма';
      return '<form id="auth-form-register-verify" novalidate>' +
        '<div class="hint">Код подтверждения отправлен на <b>' +
          OM.esc((regPending && regPending.target) || '') + '</b> (' + ch + ').</div>' +
        devBlock(regPending && regPending.dev_code) +
        '<div class="field"><label>Код подтверждения</label>' +
          '<input class="input" name="code" data-code-input inputmode="numeric" maxlength="6" autocomplete="one-time-code" placeholder="000000"></div>' +
        '<button class="btn primary wide" type="submit">Подтвердить и войти</button>' +
        '<div class="err hidden"></div>' +
        '<div class="hint"><a href="#" data-act="reg-back">' + OM.icon('arrow-left', 14) + ' Изменить данные</a></div>' +
        '</form>';
    }
    const isEmail = regMethod === 'email';
    return '<div class="method-grid">' +
        '<button type="button" class="method-btn' + (isEmail ? ' active' : '') + '" data-regmethod="email">' + OM.icon('mail', 18) + 'Почта</button>' +
        '<button type="button" class="method-btn' + (!isEmail ? ' active' : '') + '" data-regmethod="phone">' + OM.icon('phone', 18) + 'Телефон</button>' +
      '</div>' +
      '<form id="auth-form-register" novalidate>' +
        '<div class="field"><label>Логин</label>' +
          '<input class="input" name="username" type="text" autocomplete="username" placeholder="orange_user">' +
          '<div class="hint" id="reg-un-hint">3–32 символа: латиница, цифры, «_» и «.»</div></div>' +
        '<div class="field"><label>Отображаемое имя</label>' +
          '<input class="input" name="display_name" type="text" autocomplete="nickname" placeholder="Как вас показывать"></div>' +
        '<div class="field"><label>' + (isEmail ? 'Электронная почта' : 'Номер телефона') + '</label>' +
          '<input class="input" name="contact" type="' + (isEmail ? 'email' : 'tel') + '" autocomplete="' + (isEmail ? 'email' : 'tel') + '" ' +
            'placeholder="' + (isEmail ? 'you@example.com' : '+7 900 000-00-00') + '">' +
          '<div class="hint" id="reg-ct-hint">' + (isEmail ? 'На неё придёт код подтверждения' : 'На него придёт код подтверждения') + '</div></div>' +
        '<div class="field"><label>Пароль (минимум 8 символов)</label>' +
          '<input class="input" name="password" type="password" autocomplete="new-password" placeholder="••••••••">' +
          '<div class="hint" id="reg-pw-hint">Минимум 8 символов</div></div>' +
        '<button class="btn primary wide" type="submit">Создать аккаунт</button>' +
        '<div class="err hidden"></div>' +
      '</form>';
  }

  /* ------------------------------------------------------------------ разметка: восстановление пароля */
  function resetPanel() {
    if (resetStage === 'verify') {
      return '<form id="auth-form-reset-verify" novalidate>' +
        '<div class="hint">Код для смены пароля отправлен на <b>' + OM.esc(resetIdent) + '</b>.</div>' +
        devBlock(resetDev) +
        '<div class="field"><label>Код из письма или SMS</label>' +
          '<input class="input" name="code" data-code-input inputmode="numeric" maxlength="6" autocomplete="one-time-code" placeholder="000000"></div>' +
        '<div class="field"><label>Новый пароль (минимум 8 символов)</label>' +
          '<input class="input" name="new_password" type="password" autocomplete="new-password" placeholder="••••••••"></div>' +
        '<button class="btn primary wide" type="submit">Сменить пароль и войти</button>' +
        '<div class="err hidden"></div>' +
        '<div class="hint"><a href="#" data-act="login-back">' + OM.icon('arrow-left', 14) + ' Вернуться ко входу</a></div>' +
        '</form>';
    }
    return '<form id="auth-form-reset" novalidate>' +
      '<div class="hint">Укажите почту или телефон аккаунта — пришлём код для смены пароля.</div>' +
      '<div class="field"><label>Почта или телефон</label>' +
        '<input class="input" name="identifier" type="text" autocomplete="username" placeholder="you@example.com"></div>' +
      '<button class="btn primary wide" type="submit">Получить код</button>' +
      '<div class="err hidden"></div>' +
      '<div class="hint"><a href="#" data-act="login-back">' + OM.icon('arrow-left', 14) + ' Вернуться ко входу</a></div>' +
      '</form>';
  }

  /* ------------------------------------------------------------------ обработчики */
  async function doPasswordLogin(form) {
    const identifier = fv(form,'identifier').trim();
    const password = fv(form,'password');
    if (!identifier) { ff(form,'identifier'); throw { message: 'Укажите почту, телефон, логин или Orange ID' }; }
    if (!password) { ff(form,'password'); throw { message: 'Введите пароль' }; }
    const res = await OM.api.post('/api/auth/login', { identifier: identifier, password: password });
    if (res && res.need_totp) {
      // Второй фактор: показываем шаг с кодом аутентификатора.
      totpChallenge = res.challenge || '';
      loginMethod = 'totp';
      render();
      OM.toast('Введите 6-значный код из приложения-аутентификатора', 'ok', 5000);
      return;
    }
    finish(res);
  }

  async function doTotpVerify(form) {
    const code = fv(form,'code').trim();
    if (!/^\d{4,8}$/.test(code)) throw { message: 'Введите 6-значный код из приложения' };
    const res = await OM.api.post('/api/auth/login/totp', { challenge: totpChallenge, code: code });
    finish(res);
  }

  async function doOtpRequest(form) {
    const identifier = fv(form,'identifier').trim();
    if (!identifier) { ff(form,'identifier'); throw { message: 'Укажите номер телефона' }; }
    const res = await OM.api.post('/api/auth/otp/request', { identifier: identifier });
    otpIdent = identifier;
    otpDev = (res && res.dev_code) || '';
    otpStage = 'verify';
    render();
    if (!otpDev) OM.toast('Если аккаунт существует, код отправлен на ' + ((res && res.target) || identifier), 'ok', 5000);
  }

  async function doOtpVerify(form) {
    const code = fv(form,'code').trim();
    if (code.length < 4) throw { message: 'Введите код из SMS' };
    const res = await OM.api.post('/api/auth/otp/verify', { identifier: otpIdent, code: code });
    finish(res);
  }

  async function doFlashLogin(form) {
    if (!OM.flashKey || !OM.flashKey.pick) throw { message: 'Флеш-ключи не поддерживаются браузером' };
    let picked;
    try {
      picked = await OM.flashKey.pick();
    } catch (e) {
      if (e && e.name === 'AbortError') return; // пользователь просто закрыл диалог
      throw { message: 'Не удалось прочитать файл ключа' };
    }
    if (!picked || !picked.content) throw { message: 'Файл ключа не выбран' };
    const device = fv(form,'device').trim() || 'Флешка-ключ';
    const res = await OM.api.post('/api/auth/session-file/login', { content: picked.content, device: device });
    finish(res);
    // Сервер отдал новую версию ключа — перезаписываем файл на носителе.
    if (res.rotated_content) {
      let ok = false;
      try { ok = await OM.flashKey.rewrite(OM.flashKey.parseUuid(res.rotated_content), res.rotated_content); } catch (e) { ok = false; }
      if (!ok) {
        try { await OM.flashKey.save(res.rotated_content, picked.name || 'orangem-key.omkey'); } catch (e) {}
      }
      OM.toast('Файл-ключ обновлён: сервер перешифровывает его в реальном времени', 'ok', 6000);
    }
  }

  async function doRegister(form) {
    const username = fv(form,'username').trim();
    const displayName = fv(form,'display_name').trim();
    const contact = fv(form,'contact').trim();
    const password = fv(form,'password');
    if (!username) { ff(form,'username'); throw { message: 'Укажите логин' }; }
    if (!contact) { ff(form,'contact'); throw { message: regMethod === 'email' ? 'Укажите почту' : 'Укажите номер телефона' }; }
    if (password.length < 8) { ff(form,'password'); throw { message: 'Пароль должен быть не короче 8 символов' }; }
    const body = { method: regMethod, username: username, display_name: displayName, password: password };
    if (regMethod === 'email') body.email = contact; else body.phone = contact;
    const res = await OM.api.post('/api/auth/register', body);
    regPending = {
      target: (res && res.target) || contact,
      channel: (res && res.channel) || regMethod,
      dev_code: (res && res.dev_code) || ''
    };
    regStage = 'verify';
    render();
    if (regPending.dev_code) OM.toast('Демо-режим: код ' + regPending.dev_code, 'ok', 6000);
  }

  async function doRegisterVerify(form) {
    const code = fv(form,'code').trim();
    if (code.length < 4) throw { message: 'Введите код подтверждения' };
    const res = await OM.api.post('/api/auth/register/verify', {
      target: regPending ? regPending.target : '',
      code: code
    });
    finish(res);
  }

  async function doResetRequest(form) {
    const identifier = fv(form,'identifier').trim();
    if (!identifier) { ff(form,'identifier'); throw { message: 'Укажите почту или телефон' }; }
    const res = await OM.api.post('/api/auth/password/reset', { identifier: identifier });
    resetIdent = identifier;
    resetDev = (res && res.dev_code) || '';
    resetStage = 'verify';
    render();
    if (!resetDev) OM.toast('Если аккаунт существует, код отправлен на ' + ((res && res.target) || identifier), 'ok', 5000);
  }

  async function doResetVerify(form) {
    const code = fv(form,'code').trim();
    const newPassword = fv(form,'new_password');
    if (code.length < 4) throw { message: 'Введите код из письма или SMS' };
    if (newPassword.length < 8) throw { message: 'Пароль должен быть не короче 8 символов' };
    const res = await OM.api.post('/api/auth/password/reset/verify', {
      identifier: resetIdent, code: code, new_password: newPassword
    });
    finish(res);
  }

  /* ------------------------------------------------------------------ живая проверка занятости логина (debounce 400 мс) */
  function bindRegisterChecks(root) {
    const form = root.querySelector('#auth-form-register');
    if (!form) return;

    const unHint = form.querySelector('#reg-un-hint');
    const ctHint = form.querySelector('#reg-ct-hint');
    const pwHint = form.querySelector('#reg-pw-hint');

    const checkUsername = OM.debounce(async function () {
      const v = fv(form,'username').trim();
      if (v.length < 3) {
        unHint.className = 'hint';
        unHint.textContent = '3–32 символа: латиница, цифры, «_» и «.»';
        return;
      }
      try {
        const r = await OM.api.get('/api/auth/check?username=' + encodeURIComponent(v));
        if (r.username_free) { unHint.className = 'okmsg'; unHint.textContent = 'Логин свободен'; }
        else { unHint.className = 'err'; unHint.textContent = 'Этот логин уже занят'; }
      } catch (e) {
        unHint.className = 'hint';
        unHint.textContent = 'Не удалось проверить логин';
      }
    }, 400);

    const checkContact = OM.debounce(async function () {
      const v = fv(form,'contact').trim();
      const isEmail = regMethod === 'email';
      if (!v) {
        ctHint.className = 'hint';
        ctHint.textContent = isEmail ? 'На неё придёт код подтверждения' : 'На него придёт код подтверждения';
        return;
      }
      const param = isEmail ? 'email' : 'phone';
      try {
        const r = await OM.api.get('/api/auth/check?' + param + '=' + encodeURIComponent(v));
        const free = isEmail ? r.email_free : r.phone_free;
        if (free) { ctHint.className = 'okmsg'; ctHint.textContent = isEmail ? 'Почта свободна' : 'Номер свободен'; }
        else { ctHint.className = 'err'; ctHint.textContent = isEmail ? 'Почта уже привязана к аккаунту' : 'Номер уже привязан к аккаунту'; }
      } catch (e) {
        ctHint.className = 'hint';
        ctHint.textContent = 'Не удалось проверить';
      }
    }, 400);

    ff(form,'username').addEventListener('input', checkUsername);
    ff(form,'contact').addEventListener('input', checkContact);
    ff(form,'password').addEventListener('input', function () {
      const n = fv(form,'password').length;
      if (!n) { pwHint.className = 'hint'; pwHint.textContent = 'Минимум 8 символов'; }
      else if (n < 8) { pwHint.className = 'err'; pwHint.textContent = 'Ещё ' + (8 - n) + ' символ(а)'; }
      else { pwHint.className = 'okmsg'; pwHint.textContent = 'Длина пароля достаточная'; }
    });
  }

  function bindForms(root) {
    const bind = function (id, fn) {
      const f = root.querySelector('#' + id);
      if (f) formSubmit(f, f.querySelector('button[type=submit]'), function () { return fn(f); });
    };
    bind('auth-form-login', doPasswordLogin);
    bind('auth-form-phone', doPasswordLogin);
    bind('auth-form-flash', doFlashLogin);
    bind('auth-form-totp-start', doPasswordLogin);
    bind('auth-form-totp', doTotpVerify);
    bind('auth-form-otp-request', doOtpRequest);
    bind('auth-form-otp-verify', doOtpVerify);
    bind('auth-form-register', doRegister);
    bind('auth-form-register-verify', doRegisterVerify);
    bind('auth-form-reset', doResetRequest);
    bind('auth-form-reset-verify', doResetVerify);
    bindRegisterChecks(root);
  }

  /* ------------------------------------------------------------------ отрисовка */
  function render() {
    const b = authBody();
    if (!b) return;
    // Активный таб: регистрация либо вход (экран восстановления относится ко входу).
    document.querySelectorAll('.auth-tab[data-authtab]').forEach(function (t) {
      t.classList.toggle('active', t.dataset.authtab === (screen === 'register' ? 'register' : 'login'));
    });
    if (screen === 'register') b.innerHTML = registerPanel();
    else if (screen === 'reset') b.innerHTML = resetPanel();
    else b.innerHTML = loginPanel();
    bindForms(b);
  }

  /* ------------------------------------------------------------------ подписки и делегирование (один раз на страницу) */
  function bindTabs() {
    document.querySelectorAll('.auth-tab[data-authtab]').forEach(function (t) {
      if (t._omAuthBound) return;
      t._omAuthBound = true;
      t.addEventListener('click', function () {
        screen = t.dataset.authtab === 'register' ? 'register' : 'login';
        regStage = 'form';
        totpChallenge = '';
        otpStage = 'request';
        render();
      });
    });
  }

  let delegated = false;
  function bindDelegation() {
    if (delegated) return;
    delegated = true;
    document.addEventListener('click', function (e) {
      const b = authBody();
      if (!b || !b.contains(e.target)) return;

      // Кнопка «вставить демо-код» в ближайшее поле кода
      const fill = e.target.closest('[data-fill]');
      if (fill) {
        e.preventDefault();
        const inp = b.querySelector('[data-code-input]');
        if (inp) { inp.value = fill.dataset.fill; inp.focus(); }
        return;
      }
      // Выбор способа входа
      const lm = e.target.closest('[data-loginmethod]');
      if (lm) {
        loginMethod = lm.dataset.loginmethod;
        totpChallenge = '';
        otpStage = 'request';
        render();
        return;
      }
      // Переключение «пароль / одноразовый код» для телефона
      const pm = e.target.closest('[data-phone-mode]');
      if (pm) {
        phoneMode = pm.dataset.phoneMode;
        otpStage = 'request';
        render();
        return;
      }
      // Способ регистрации
      const rm = e.target.closest('[data-regmethod]');
      if (rm) {
        regMethod = rm.dataset.regmethod;
        render();
        return;
      }
      // Ссылки-действия
      const act = e.target.closest('[data-act]');
      if (act) {
        e.preventDefault();
        const a = act.dataset.act;
        if (a === 'reset') { screen = 'reset'; resetStage = 'request'; render(); }
        else if (a === 'login-back') { screen = 'login'; totpChallenge = ''; otpStage = 'request'; render(); }
        else if (a === 'reg-back') { regStage = 'form'; render(); }
        else if (a === 'otp-restart') { otpStage = 'request'; otpDev = ''; render(); }
      }
    });
  }

  bindTabs();
  bindDelegation();

  /* ------------------------------------------------------------------ регистрация раздела */
  function registerView() {
    OM.views = OM.views || {};
    OM.views.auth = {
      enter: function () {
        screen = 'login';
        loginMethod = 'email';
        phoneMode = 'password';
        otpStage = 'request';
        totpChallenge = '';
        regStage = 'form';
        resetStage = 'request';
        bindTabs();
        bindDelegation();
        render();
      },
      leave: function () {}
    };
  }

  // app.js выполняется после этого файла и заново создаёт OM.views = {},
  // поэтому повторно регистрируем раздел к моменту DOMContentLoaded (до boot()).
  registerView();
  window.addEventListener('DOMContentLoaded', registerView);
})();
