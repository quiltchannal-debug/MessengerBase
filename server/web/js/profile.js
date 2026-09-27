/* OrangeM — профиль пользователя: #view-profile → #profile-body
   Раздел регистрируется как OM.views.profile. Параметр перехода — params.id
   (числовой id | username | @username | OM-XXXX).
   Дополнительно: OM.openProfile(id) и событие OM.emit('open-profile', id). */
window.OM = window.OM || {};

(function () {
  'use strict';

  /* ------------------------------------------------------------------ состояние раздела */
  const state = {
    userId: 0,        // id открытого профиля
    user: null,       // карточка пользователя
    restricted: false,// сервер скрыл профиль (privacy)
    blockedByMe: false,
    blockedMe: false,
    cursor: '',       // next_cursor для «Показать ещё»
    postsBusy: false,
    seq: 0            // защита от гонки при быстрых переходах
  };

  /* ------------------------------------------------------------------ утилиты */
  function body() { return document.getElementById('profile-body'); }

  function isMe(u) {
    if (!u) return false;
    if (u.is_me) return true;
    return !!(OM.state.me && Number(OM.state.me.id) === Number(u.id));
  }

  function bannerStyle(u) {
    // Баннер пользователя — только через inline background-image.
    return u && u.banner
      ? ' style="background-image:url(' + OM.esc(u.banner) + ');background-size:cover;background-position:center"'
      : '';
  }

  function showError(msg) {
    const b = body();
    if (!b) return;
    b.innerHTML = '<div class="empty-state"><img class="empty-logo" src="/assets/logo.jpg" alt="">' +
      '<p>' + OM.esc(msg) + '</p></div>';
  }

  /* ------------------------------------------------------------------ разметка профиля */
  function actionsHtml(u) {
    if (state.restricted) return '';
    const mine = isMe(u);
    let html = '';
    if (mine) {
      html += '<button class="btn primary" data-act="edit">Редактировать профиль</button>';
    } else {
      html += u.i_follow
        ? '<button class="btn ghost" data-act="follow">Отписаться</button>'
        : '<button class="btn primary" data-act="follow">Подписаться</button>';
      if (u.can_dm !== false && !state.blockedByMe && !state.blockedMe) {
        html += '<button class="btn ghost" data-act="dm">Написать</button>';
      }
      html += state.blockedByMe
        ? '<button class="btn ghost" data-act="block">Разблокировать</button>'
        : '<button class="btn danger" data-act="block">Заблокировать</button>';
    }
    return '<div class="head-actions">' + html + '</div>';
  }

  function flagsHtml(u) {
    let html = '';
    if (u.i_follow) html += '<div class="hint">Вы подписаны</div>';
    if (u.follows_me) html += '<div class="hint">Подписан(а) на вас</div>';
    if (state.blockedByMe) html += '<div class="hint">Пользователь в вашем чёрном списке</div>';
    if (state.blockedMe) html += '<div class="hint">Пользователь ограничил доступ к профилю</div>';
    if (u.is_contact) html += '<div class="hint">В ваших контактах</div>';
    return html;
  }

  function heroHtml(u) {
    const online = !!(u.presence && u.presence.online);
    return '<div class="profile-hero">' +
      '<div class="banner"' + bannerStyle(u) + '></div>' +
      '<div class="inner">' +
        OM.avatar(u, { size: 'xl', online: online }) +
        '<div class="cw">' +
          '<h2>' + OM.esc(u.display_name || u.username || 'Профиль') +
            (isMe(u) ? ' <span class="badge grey">это вы</span>' : '') +
            (u.is_admin ? ' <span class="badge">админ</span>' : '') + '</h2>' +
          '<div class="kv">' +
            '<span class="k">@' + OM.esc(u.username || '') + '</span>' +
            (u.orange_id
              ? '<span class="badge orange" id="pf-orange-id">' + OM.esc(u.orange_id) + '</span>' +
                '<button class="btn small ghost" data-act="copy-id">копировать</button>'
              : '') +
            (u.short_id ? '<span class="badge grey">' + OM.esc(u.short_id) + '</span>' : '') +
          '</div>' +
          (u.bio ? '<div class="hint">' + OM.linkify(u.bio) + '</div>' : '') +
          '<div class="stat-row">' +
            '<div class="stat"><b>' + OM.fmtNum(u.posts || 0) + '</b><span>публикаций</span></div>' +
            '<div class="stat"><b>' + OM.fmtNum(u.followers || 0) + '</b><span>подписчиков</span></div>' +
            '<div class="stat"><b>' + OM.fmtNum(u.following || 0) + '</b><span>подписок</span></div>' +
          '</div>' +
          '<div class="kv">' +
            '<span class="k">регистрация:</span>' +
            '<span>' + (u.created_at ? OM.esc(OM.fmtDateTime(u.created_at)) : '—') + '</span>' +
            '<span class="k">•</span>' +
            '<span id="pf-presence">' + OM.esc(OM.onlineText(u)) + '</span>' +
          '</div>' +
          flagsHtml(u) +
        '</div>' +
        actionsHtml(u) +
      '</div>' +
    '</div>';
  }

  function restrictedHtml(u) {
    const online = !!(u.presence && u.presence.online);
    return '<div class="profile-hero">' +
      '<div class="banner"' + bannerStyle(u) + '></div>' +
      '<div class="inner">' +
        OM.avatar(u, { size: 'xl', online: online }) +
        '<div class="cw">' +
          '<h2>' + OM.esc(u.display_name || u.username || 'Профиль') + '</h2>' +
          '<div class="kv">' +
            '<span class="k">@' + OM.esc(u.username || '') + '</span>' +
            (u.orange_id ? '<span class="badge orange">' + OM.esc(u.orange_id) + '</span>' : '') +
          '</div>' +
          '<div class="hint">' + OM.icon('lock', 14) + ' Профиль закрыт настройками приватности: видны только имя, логин и Orange ID.</div>' +
        '</div>' +
      '</div>' +
    '</div>';
  }

  /** Компактный локальный рендер поста (feed.js не дублируем, но карточку собираем сами). */
  function postCard(p) {
    const a = p.author || {};
    let media = '';
    if (p.media) {
      media = p.media_kind === 'video'
        ? '<div class="post-media"><video src="' + OM.esc(p.media) + '" controls preload="metadata"></video></div>'
        : '<div class="post-media"><img src="' + OM.esc(p.media) + '" alt="" loading="lazy"></div>';
    }
    let tags = '';
    if (p.tags && p.tags.length) {
      tags = '<div class="post-tags">' + p.tags.map(function (t) {
        return '<span class="tag">#' + OM.esc(t) + '</span>';
      }).join('') + '</div>';
    }
    return '<article class="post-card" data-post="' + OM.esc(p.id) + '">' +
      '<div class="post-head" data-author="' + OM.esc(a.id) + '">' +
        OM.avatar(a, { size: 'sm' }) +
        '<div class="who">' +
          '<div class="nm">' + OM.esc(a.display_name || a.username || 'Пользователь') +
            (p.community ? ' <span class="badge">' + OM.esc(p.community.name || '') + '</span>' : '') + '</div>' +
          '<div class="sub"><span>@' + OM.esc(a.username || '') + '</span>' +
            '<span>' + OM.esc(OM.timeAgo(p.created_at)) + '</span>' +
            (p.visibility && p.visibility !== 'public'
              ? '<span class="badge grey">' + OM.esc(p.visibility) + '</span>' : '') +
            (p.edited_at ? '<span>изменён</span>' : '') +
          '</div>' +
        '</div>' +
      '</div>' +
      (p.title ? '<div class="post-title">' + OM.esc(p.title) + '</div>' : '') +
      (p.body ? '<div class="post-body">' + OM.linkify(p.body) + '</div>' : '') +
      media + tags +
      '<div class="post-actions">' +
        '<button class="post-action' + (p.liked ? ' on' : '') + '" data-act="like" data-id="' + OM.esc(p.id) + '">' +
          OM.icon('heart', 16) + ' <b>' + OM.fmtNum(p.likes || 0) + '</b></button>' +
        '<span class="badge grey">' + OM.icon('comment', 14) + ' ' + OM.fmtNum(p.comments || 0) + '</span>' +
        '<span class="badge grey">' + OM.icon('eye', 14) + ' ' + OM.fmtNum(p.views || 0) + '</span>' +
      '</div>' +
    '</article>';
  }

  function postsSectionHtml() {
    if (state.restricted) return '';
    return '<h3>Публикации</h3>' +
      '<div id="pf-posts" class="feed-list"></div>' +
      '<button class="btn ghost wide hidden" id="pf-more" data-act="more">Показать ещё</button>';
  }

  function render() {
    const b = body();
    if (!b) return;
    const u = state.user || {};
    b.innerHTML = (state.restricted ? restrictedHtml(u) : heroHtml(u)) + postsSectionHtml();
  }

  function renderPosts(posts, append) {
    const list = document.getElementById('pf-posts');
    if (!list) return;
    const html = posts.map(postCard).join('');
    if (append) list.insertAdjacentHTML('beforeend', html);
    else list.innerHTML = html || '<div class="empty-state"><p>Публикаций пока нет.</p></div>';
    const more = document.getElementById('pf-more');
    if (more) more.classList.toggle('hidden', !state.cursor);
  }

  /* ------------------------------------------------------------------ загрузка данных */
  async function loadPosts(append) {
    if (state.restricted || state.postsBusy || !state.userId) return;
    state.postsBusy = true;
    const more = document.getElementById('pf-more');
    if (more) more.disabled = true;
    try {
      const url = '/api/feed?scope=user&user=' + encodeURIComponent(state.userId) + '&limit=20' +
        (append && state.cursor ? '&cursor=' + encodeURIComponent(state.cursor) : '');
      const r = await OM.api.get(url);
      state.cursor = r.next_cursor || '';
      renderPosts(r.posts || [], !!append);
    } catch (e) {
      OM.toast(e.message, 'err');
    } finally {
      state.postsBusy = false;
      if (more) more.disabled = false;
    }
  }

  async function load(id) {
    const seq = ++state.seq;
    const b = body();
    if (b) {
      b.innerHTML = '<div class="empty-state"><img class="empty-logo" src="/assets/logo.jpg" alt="">' +
        '<p>Загружаем профиль…</p></div>';
    }
    let res;
    try {
      res = await OM.api.get('/api/users/' + encodeURIComponent(id));
    } catch (e) {
      if (seq !== state.seq) return;
      showError(e.message || 'Не удалось открыть профиль');
      return;
    }
    if (seq !== state.seq) return;
    const u = res.user || {};
    state.user = u;
    state.userId = u.id || id;
    state.restricted = !!res.restricted;
    state.blockedByMe = res.is_blocked_by_me !== undefined ? !!res.is_blocked_by_me : !!u.blocked;
    state.blockedMe = !!res.blocked_me;
    state.cursor = '';
    render();
    if (!state.restricted) loadPosts(false);
  }

  /* ------------------------------------------------------------------ действия */
  async function toggleFollow() {
    const u = state.user;
    if (!u) return;
    try {
      if (u.i_follow) {
        await OM.api.del('/api/users/' + encodeURIComponent(u.id) + '/follow');
        u.i_follow = false;
        u.followers = Math.max(0, (u.followers || 0) - 1);
        OM.toast('Вы отписались от @' + u.username, 'ok');
      } else {
        await OM.api.post('/api/users/' + encodeURIComponent(u.id) + '/follow');
        u.i_follow = true;
        u.followers = (u.followers || 0) + 1;
        OM.toast('Вы подписались на @' + u.username, 'ok');
      }
      render();
      loadPosts(false);
    } catch (e) {
      OM.toast(e.message, 'err');
    }
  }

  async function toggleBlock() {
    const u = state.user;
    if (!u) return;
    const doIt = async function () {
      try {
        if (state.blockedByMe) {
          await OM.api.del('/api/users/' + encodeURIComponent(u.id) + '/block');
          state.blockedByMe = false;
          u.blocked = false;
          OM.toast('Пользователь разблокирован', 'ok');
        } else {
          await OM.api.post('/api/users/' + encodeURIComponent(u.id) + '/block');
          state.blockedByMe = true;
          u.blocked = true;
          u.i_follow = false;
          u.follows_me = false;
          OM.toast('Пользователь заблокирован', 'ok');
        }
        render();
      } catch (e) {
        OM.toast(e.message, 'err');
      }
    };
    if (state.blockedByMe) doIt();
    else OM.confirm('Заблокировать @' + u.username + '? Подписки будут удалены.', doIt, 'Заблокировать');
  }

  async function writeMessage() {
    const u = state.user;
    if (!u) return;
    try {
      const r = await OM.api.post('/api/chats', { kind: 'dm', identifier: u.username });
      const chat = r.chat || r;
      OM.navigate('chats');
      if (chat && chat.id) OM.emit('open-chat', chat.id);
    } catch (e) {
      OM.toast(e.message, 'err');
    }
  }

  async function toggleLike(btn) {
    const id = btn.dataset.id;
    const on = btn.classList.contains('on');
    btn.disabled = true;
    try {
      const r = on
        ? await OM.api.del('/api/posts/' + encodeURIComponent(id) + '/like')
        : await OM.api.post('/api/posts/' + encodeURIComponent(id) + '/like');
      btn.classList.toggle('on', !!r.liked);
      const b = btn.querySelector('b');
      if (b) b.textContent = OM.fmtNum(r.likes || 0);
    } catch (e) {
      OM.toast(e.message, 'err');
    } finally {
      btn.disabled = false;
    }
  }

  /* ------------------------------------------------------------------ редактирование своего профиля */
  function pickImage(onUrl) {
    const inp = document.createElement('input');
    inp.type = 'file';
    inp.accept = 'image/*';
    inp.onchange = async function () {
      const f = inp.files && inp.files[0];
      if (!f) return;
      try {
        const r = await OM.api.upload(f, 'image');
        onUrl(r.url);
        OM.toast('Файл загружен', 'ok');
      } catch (e) {
        OM.toast(e.message, 'err');
      }
    };
    inp.click();
  }

  function openEdit() {
    const u = state.user || OM.state.me || {};
    const html =
      '<div class="field"><label>Отображаемое имя</label>' +
        '<input class="input" id="pf-e-name" type="text" value="' + OM.esc(u.display_name || '') + '"></div>' +
      '<div class="field"><label>Логин</label>' +
        '<input class="input" id="pf-e-username" type="text" value="' + OM.esc(u.username || '') + '"></div>' +
      '<div class="field"><label>О себе</label>' +
        '<textarea class="input" id="pf-e-bio" rows="3">' + OM.esc(u.bio || '') + '</textarea></div>' +
      '<div class="field"><label>Аватар (URL)</label><div class="form-row">' +
        '<input class="input" id="pf-e-avatar" type="text" value="' + OM.esc(u.avatar || '') + '">' +
        '<button class="btn ghost" type="button" id="pf-e-avatar-up">Загрузить</button></div></div>' +
      '<div class="field"><label>Баннер (URL)</label><div class="form-row">' +
        '<input class="input" id="pf-e-banner" type="text" value="' + OM.esc(u.banner || '') + '">' +
        '<button class="btn ghost" type="button" id="pf-e-banner-up">Загрузить</button></div></div>' +
      '<div class="err hidden"></div>';

    const m = OM.modal({
      title: 'Редактировать профиль',
      body: html,
      actions: [
        { label: 'Отмена', cls: 'ghost' },
        { label: 'Сохранить', cls: 'primary', onClick: async function (modal) {
            const err = modal.body.querySelector('.err');
            const btn = modal.root.querySelector('.modal-foot .primary');
            const payload = {
              display_name: modal.body.querySelector('#pf-e-name').value.trim(),
              username: modal.body.querySelector('#pf-e-username').value.trim(),
              bio: modal.body.querySelector('#pf-e-bio').value,
              avatar: modal.body.querySelector('#pf-e-avatar').value.trim(),
              banner: modal.body.querySelector('#pf-e-banner').value.trim()
            };
            if (btn) btn.disabled = true;
            try {
              const r = await OM.api.patch('/api/users/me', payload);
              if (r.user) {
                OM.setMe(r.user);
                state.user = r.user;
                state.userId = r.user.id;
              }
              render();
              loadPosts(false);
              OM.toast('Профиль обновлён', 'ok');
              modal.close();
            } catch (e) {
              err.textContent = e.message || 'Не удалось сохранить';
              err.classList.remove('hidden');
              OM.toast(e.message, 'err');
            } finally {
              if (btn) btn.disabled = false;
            }
          } }
      ]
    });

    const up = m.body.querySelector('#pf-e-avatar-up');
    if (up) up.onclick = function () {
      pickImage(function (url) { m.body.querySelector('#pf-e-avatar').value = url; });
    };
    const upB = m.body.querySelector('#pf-e-banner-up');
    if (upB) upB.onclick = function () {
      pickImage(function (url) { m.body.querySelector('#pf-e-banner').value = url; });
    };
  }

  /* ------------------------------------------------------------------ делегирование кликов внутри профиля */
  const bound = { click: false, subs: false };

  function bindClick() {
    if (bound.click) return;
    bound.click = true;
    document.addEventListener('click', function (e) {
      const b = body();
      if (!b || !b.contains(e.target)) return;

      const like = e.target.closest('[data-act="like"]');
      if (like) { e.preventDefault(); toggleLike(like); return; }

      const author = e.target.closest('[data-author]');
      if (author && author.dataset.author) {
        OM.openProfile(author.dataset.author);
        return;
      }

      const el = e.target.closest('[data-act]');
      if (!el) return;
      const act = el.dataset.act;
      if (act === 'follow') { e.preventDefault(); toggleFollow(); }
      else if (act === 'block') { e.preventDefault(); toggleBlock(); }
      else if (act === 'dm') { e.preventDefault(); writeMessage(); }
      else if (act === 'edit') { e.preventDefault(); openEdit(); }
      else if (act === 'more') { e.preventDefault(); loadPosts(true); }
      else if (act === 'copy-id') {
        e.preventDefault();
        if (state.user && state.user.orange_id) OM.copy(state.user.orange_id);
      }
    });
  }

  /* ------------------------------------------------------------------ подписки (один раз на страницу) */
  function bindSubs() {
    if (bound.subs) return;
    bound.subs = true;

    // Переход в профиль из любого модуля
    OM.on('open-profile', function (id) {
      if (id !== undefined && id !== null && id !== '') OM.openProfile(id);
    });

    // Присутствие в реальном времени: обновляем строку только для открытого профиля
    OM.on('ws:presence', function (d) {
      if (!d || !state.user) return;
      if (Number(d.user_id) !== Number(state.userId)) return;
      const p = state.user.presence = state.user.presence || {};
      p.online = !!d.online;
      if (d.last_seen) p.last_seen = d.last_seen;
      const line = document.getElementById('pf-presence');
      if (line) line.textContent = OM.onlineText(state.user);
      const dot = document.querySelector('#profile-body .presence-dot');
      if (dot) dot.classList.toggle('online', !!d.online);
    });

    // Пользователь обновил свой профиль в настройках — перерисовываем, если это он
    OM.on('ws:user.updated', function (d) {
      const u = d && (d.user || d);
      if (!u || !state.user) return;
      if (Number(u.id) !== Number(state.userId)) return;
      Object.assign(state.user, u);
      render();
      loadPosts(false);
    });
  }

  bindClick();
  bindSubs();

  /* ------------------------------------------------------------------ регистрация раздела */
  function registerView() {
    OM.views = OM.views || {};
    OM.views.profile = {
      enter: function (params) {
        bindClick();
        bindSubs();
        const me = OM.state.me;
        const id = (params && params.id !== undefined && params.id !== null && params.id !== '')
          ? params.id
          : (me ? me.id : 0);
        state.seq++;
        state.user = null;
        state.restricted = false;
        state.cursor = '';
        if (!id) { showError('Профиль не выбран'); return; }
        load(id);
      },
      leave: function () {
        state.seq++;   // отменяем «догоняющие» ответы
      }
    };
  }

  // app.js выполняется после этого файла и пересоздаёт OM.views = {},
  // поэтому регистрируем раздел повторно к DOMContentLoaded (раньше boot()).
  registerView();
  window.addEventListener('DOMContentLoaded', registerView);

  /* Публичный помощник для других модулей */
  OM.openProfile = function (id) { OM.navigate('profile', { id: id }); };
})();
