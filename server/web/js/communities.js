/* OrangeM — сообщества: список, создание, вход по коду, страница сообщества,
   участники и роли, приглашения, публикации сообщества.
   Регистрирует OM.views.communities (#community-list) и OM.views.community (#community-detail). */
window.OM = window.OM || {};

(function () {
  'use strict';

  /* ======================================================================= состояние */
  const listState = {
    scope: 'all',      // all | mine
    q: '',
    bound: false,      // обработчики списка навешены
    joinedOnce: false, // авто-вход по /join/<code> выполнен
    reqId: 0
  };

  const cur = {
    key: null,         // id | slug | OMG-XXXX
    community: null,
    members: [],
    posts: [],
    cursor: 0,
    loading: false,
    memberFilter: '',
    reqId: 0,
    bound: false
  };

  let composerMedia = { url: '', kind: '', name: '' }; // вложение композера сообщества

  const $ = id => document.getElementById(id);

  /* ======================================================================= утилиты */
  const esc = s => OM.esc(s);

  /** Объект-«пользователь» для OM.avatar из объекта сообщества */
  function asAuthor(c) {
    return { id: c.id, avatar: c.avatar, display_name: c.name, username: c.slug };
  }

  function isOwner(c) {
    return !!(c && c.owner && c.owner.id && OM.state.me && c.owner.id === OM.state.me.id);
  }

  /** Владелец, админ сообщества или администратор OrangeM */
  function canManage(c) {
    if (!c) return false;
    if (isOwner(c)) return true;
    if (c.my_role === 'admin') return true;
    return !!(OM.state.me && OM.state.me.is_admin);
  }

  /** Диалог выбора файла → Promise<File|null> */
  function pickFile(accept) {
    return new Promise(resolve => {
      const inp = document.createElement('input');
      inp.type = 'file';
      if (accept) inp.accept = accept;
      inp.onchange = () => resolve((inp.files && inp.files[0]) || null);
      inp.click();
    });
  }

  /** Кинд вложения по типу файла */
  function mediaKind(file) {
    return String(file.type || '').indexOf('video') === 0 ? 'video' : 'image';
  }

  /** Slug из названия (с транслитерацией кириллицы), как у сервера */
  const TRANSLIT = {
    а: 'a', б: 'b', в: 'v', г: 'g', д: 'd', е: 'e', ё: 'e', ж: 'zh', з: 'z', и: 'i',
    й: 'y', к: 'k', л: 'l', м: 'm', н: 'n', о: 'o', п: 'p', р: 'r', с: 's', т: 't',
    у: 'u', ф: 'f', х: 'h', ц: 'c', ч: 'ch', ш: 'sh', щ: 'sch', ъ: '', ы: 'y', ь: '',
    э: 'e', ю: 'yu', я: 'ya'
  };

  function slugify(s) {
    return String(s || '').toLowerCase()
      .split('').map(ch => (TRANSLIT[ch] !== undefined ? TRANSLIT[ch] : ch)).join('')
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .slice(0, 40);
  }

  function kindBadge(c) {
    return c.kind === 'channel'
      ? '<span class="badge">канал</span>'
      : '<span class="badge grey">группа</span>';
  }

  /** Открыть чат сообщества (OM.openChat предоставляет chats.js) */
  function openChat(chatId) {
    if (!chatId) { OM.toast('У сообщества пока нет чата', 'err'); return; }
    if (typeof OM.openChat === 'function') { OM.openChat(chatId); return; }
    OM.emit('open-chat', chatId);
    OM.navigate('chats');
  }

  /* ======================================================================= СПИСОК */
  function cardHtml(c) {
    const banner = c.banner
      ? ` style="background-image:url('${esc(c.banner)}');background-size:cover;background-position:center"`
      : '';
    const member = c.is_member ? ' <span class="badge orange">вы участник</span>' : '';
    return `<div class="comm-card" data-id="${c.id}">
      <div class="comm-banner"${banner}></div>
      <div class="comm-body">
        ${OM.avatar(asAuthor(c), { size: 'lg' })}
        <div class="cw">
          <div class="comm-name">${esc(c.name)} ${kindBadge(c)}${member}</div>
          <div class="comm-desc">${esc(c.description || 'Без описания')}</div>
          <div class="comm-meta">
            <span>${OM.icon('users', 14)} ${OM.fmtNum(c.members)} участников</span>
            <span>${OM.icon('edit', 14)} ${OM.fmtNum(c.posts)} публикаций</span>
            <span>${esc(c.orange_id || '')}</span>
          </div>
        </div>
      </div>
    </div>`;
  }

  async function loadList() {
    const box = $('community-list');
    if (!box) return;
    const token = ++listState.reqId;
    box.innerHTML = '<div class="panel">Загрузка сообществ…</div>';
    try {
      const url = '/api/communities?scope=' + encodeURIComponent(listState.scope) +
                  (listState.q ? '&q=' + encodeURIComponent(listState.q) : '');
      const r = await OM.api.get(url);
      if (token !== listState.reqId) return;
      const items = r.communities || [];
      box.innerHTML = items.length
        ? items.map(cardHtml).join('')
        : `<div class="empty-state">
             <img src="/assets/logo.jpg" alt="" class="empty-logo">
             <p>${listState.q ? 'Ничего не найдено. Попробуйте другое название, ссылку или ID.' : 'Сообществ пока нет. Создайте первое!'}</p>
           </div>`;
    } catch (e) {
      if (token !== listState.reqId) return;
      box.innerHTML = `<div class="panel"><div class="err">${esc(e.message || 'Не удалось загрузить сообщества')}</div></div>`;
    }
  }

  function syncTabs() {
    document.querySelectorAll('#community-tabs .chip[data-scope]').forEach(ch => {
      ch.classList.toggle('active', ch.dataset.scope === listState.scope);
    });
  }

  /** Обработчики списка навешиваются один раз (узлы статичны в index.html) */
  function bindList() {
    if (listState.bound) return;
    listState.bound = true;

    const tabs = $('community-tabs');
    if (tabs) {
      tabs.addEventListener('click', e => {
        const chip = e.target.closest('.chip[data-scope]');
        if (!chip) return;
        listState.scope = chip.dataset.scope === 'mine' ? 'mine' : 'all';
        syncTabs();
        loadList();
      });
    }

    const search = $('community-search');
    const run = () => {
      listState.q = (search.value || '').trim();
      loadList();
    };
    if (search) {
      search.addEventListener('input', OM.debounce(run, 350));
      search.addEventListener('keydown', e => { if (e.key === 'Enter') run(); });
    }
    const searchBtn = $('community-search-btn');
    if (searchBtn) searchBtn.onclick = run;

    const newBtn = $('community-new-btn');
    if (newBtn) newBtn.onclick = openCreate;

    const joinBtn = $('community-join-code');
    if (joinBtn) joinBtn.onclick = askJoinCode;

    const box = $('community-list');
    if (box) {
      box.addEventListener('click', e => {
        const card = e.target.closest('.comm-card[data-id]');
        if (card) OM.openCommunity(card.dataset.id);
      });
    }
  }

  /* ======================================================================= СОЗДАНИЕ */
  function openCreate() {
    const m = OM.modal({
      title: 'Новое сообщество',
      body: `
        <div class="field"><label>Название</label>
          <input class="input" id="cn-name" maxlength="80" placeholder="Например, OrangeM Разработка"></div>
        <div class="field"><label>Ссылка (slug)</label>
          <input class="input" id="cn-slug" placeholder="orangem-razrabotka">
          <div class="hint">Генерируется из названия. Латиница, цифры и дефис.</div></div>
        <div class="field"><label>Описание</label>
          <textarea id="cn-desc" maxlength="500" placeholder="О чём сообщество?"></textarea></div>
        <div class="field"><label>Тип</label>
          <div class="head-chips" id="cn-kind">
            <button class="chip active" data-kind="group">Группа</button>
            <button class="chip" data-kind="channel">Канал</button>
          </div></div>
        <div class="switch"><div class="lb">Открытое сообщество<small>можно вступить без приглашения</small></div>
          <button class="toggle on" id="cn-public"></button></div>
        <div class="field" style="margin-top:12px"><label>Аватар</label>
          <div class="form-row">
            <button class="btn ghost" id="cn-avatar-btn">Загрузить изображение</button>
            <span class="hint" id="cn-avatar-hint">файл не выбран</span>
          </div></div>
        <div class="err hidden" id="cn-err"></div>`,
      actions: [
        { label: 'Отмена', cls: 'ghost' },
        { label: 'Создать', cls: 'primary', onClick: submitCreate }
      ]
    });

    const nameI = m.body.querySelector('#cn-name');
    const slugI = m.body.querySelector('#cn-slug');
    const kindBox = m.body.querySelector('#cn-kind');
    const publicT = m.body.querySelector('#cn-public');
    const errBox = m.body.querySelector('#cn-err');
    let slugTouched = false;
    let avatar = '';

    // Автогенерация slug из названия, пока пользователь его не правил вручную
    nameI.addEventListener('input', () => {
      if (!slugTouched) slugI.value = slugify(nameI.value);
    });
    slugI.addEventListener('input', () => { slugTouched = true; });

    kindBox.addEventListener('click', e => {
      const chip = e.target.closest('.chip[data-kind]');
      if (!chip) return;
      kindBox.querySelectorAll('.chip').forEach(c => c.classList.toggle('active', c === chip));
    });
    publicT.onclick = () => publicT.classList.toggle('on');

    m.body.querySelector('#cn-avatar-btn').onclick = async () => {
      const file = await pickFile('image/*');
      if (!file) return;
      const hint = m.body.querySelector('#cn-avatar-hint');
      hint.textContent = 'загрузка…';
      try {
        const up = await OM.api.upload(file, 'image');
        avatar = up.url;
        hint.textContent = file.name;
      } catch (e) {
        hint.textContent = 'не выбран';
        OM.toast(e.message || 'Не удалось загрузить файл', 'err');
      }
    };

    async function submitCreate(modal) {
      const name = nameI.value.trim();
      const err = errBox;
      err.classList.add('hidden');
      if (name.length < 2) {
        err.textContent = 'Название: от 2 до 80 символов';
        err.classList.remove('hidden');
        return;
      }
      const kindChip = kindBox.querySelector('.chip.active');
      const body = {
        name,
        slug: slugI.value.trim() || slugify(name),
        description: m.body.querySelector('#cn-desc').value.trim(),
        kind: kindChip ? kindChip.dataset.kind : 'group',
        is_public: publicT.classList.contains('on'),
        avatar
      };
      try {
        const r = await OM.api.post('/api/communities', body);
        modal.close();
        OM.toast('Сообщество «' + (r.community ? r.community.name : name) + '» создано', 'ok');
        if (r.community) OM.openCommunity(r.community.id);
        else loadList();
      } catch (e) {
        err.textContent = e.message || 'Не удалось создать сообщество';
        err.classList.remove('hidden');
      }
    }

    setTimeout(() => nameI.focus(), 60);
  }

  /* ======================================================================= ВХОД ПО КОДУ */
  async function joinByCode(code) {
    if (!code) return false;
    try {
      const r = await OM.api.post('/api/communities/join/' + encodeURIComponent(code), {});
      OM.toast('Вы вступили в «' + (r.community ? r.community.name : 'сообщество') + '»', 'ok');
      if (r.community) OM.openCommunity(r.community.id);
      return true;
    } catch (e) {
      OM.toast(e.message || 'Не удалось войти по коду', 'err');
      return false;
    }
  }

  function askJoinCode() {
    OM.prompt({
      title: 'Вход по коду',
      label: 'Код приглашения',
      placeholder: 'например, a1b2c3d4e5',
      ok: 'Войти',
      hint: 'Код выдаёт администратор сообщества кнопкой «Пригласить».'
    }, async value => {
      const code = String(value || '').trim();
      if (!code) throw { message: 'Введите код' };
      const ok = await joinByCode(code);
      if (!ok) throw { message: 'Код не подошёл — проверьте его и попробуйте снова' };
    });
  }

  /** Ссылка вида /join/<code> — автоматический вход при заходе в раздел */
  function maybeJoinFromPath() {
    if (listState.joinedOnce) return;
    const m = /^\/join\/([A-Za-z0-9_-]+)/.exec(location.pathname || '');
    if (!m) return;
    listState.joinedOnce = true;
    joinByCode(m[1]).then(() => {
      try { history.replaceState(null, '', '#communities'); } catch (e) {}
    });
  }

  /* ======================================================================= СТРАНИЦА СООБЩЕСТВА */
  async function loadCommunity(key) {
    const box = $('community-detail');
    if (!box) return;
    const token = ++cur.reqId;
    box.innerHTML = '<div class="panel">Загрузка сообщества…</div>';
    try {
      const r = await OM.api.get('/api/communities/' + encodeURIComponent(key));
      if (token !== cur.reqId) return;
      cur.community = r.community;
      cur.members = r.members || [];
      cur.posts = [];
      cur.cursor = 0;
      renderDetail();
      loadPosts(true);
    } catch (e) {
      if (token !== cur.reqId) return;
      cur.community = null;
      box.innerHTML = `<div class="panel">
        <h3>Сообщество недоступно</h3>
        <div class="desc">${esc(e.message || 'Не удалось открыть сообщество')}</div>
        <button class="btn ghost" data-act="back">${OM.icon('arrow-left', 16)} Все сообщества</button>
      </div>`;
    }
  }

  function heroHtml(c) {
    const mine = !!c.is_member;
    const admin = canManage(c);
    const owner = isOwner(c);

    const banner = c.banner
      ? ` style="background-image:url('${esc(c.banner)}');background-size:cover;background-position:center"`
      : '';

    let actions = '';
    if (mine) actions += '<button class="btn ghost" data-act="leave">Покинуть</button>';
    else actions += '<button class="btn primary" data-act="join">Вступить</button>';
    if (c.chat_id) actions += '<button class="btn" data-act="chat">Открыть чат</button>';
    if (admin) actions += '<button class="btn ghost" data-act="invite">Пригласить</button>';
    if (admin) actions += '<button class="btn ghost" data-act="settings">Настройки</button>';
    if (admin) actions += '<button class="btn ghost small" data-act="avatar">Аватар</button>';
    if (admin) actions += '<button class="btn ghost small" data-act="banner">Баннер</button>';
    if (owner) actions += '<button class="btn danger" data-act="delete">Удалить</button>';

    const badges = [];
    badges.push(kindBadge(c));
    if (mine) badges.push('<span class="badge orange">вы участник</span>');
    badges.push(c.is_public
      ? '<span class="badge grey">открытое</span>'
      : '<span class="badge grey">закрытое</span>');

    return `<div class="comm-hero">
      <div class="banner"${banner}></div>
      <div class="inner">
        ${OM.avatar(asAuthor(c), { size: 'xl' })}
        <div class="cw">
          <h2>${esc(c.name)} ${badges.join(' ')}</h2>
          <div class="comm-meta">
            <span>@${esc(c.slug)}</span>
            <span>${esc(c.orange_id || '')}</span>
            <span>создано ${esc(c.created_at ? OM.fmtDateTime(c.created_at) : '')}</span>
          </div>
          <div class="comm-desc" style="max-height:none">${esc(c.description || 'Без описания')}</div>
          <div class="comm-meta">
            <span>${OM.icon('users', 14)} <b id="comm-members-count">${OM.fmtNum(c.members)}</b> участников</span>
            <span>${OM.icon('edit', 14)} ${OM.fmtNum(c.posts)} публикаций</span>
            ${c.owner ? `<span>владелец: ${esc(c.owner.display_name || c.owner.username || '')}</span>` : ''}
          </div>
        </div>
        <div class="head-actions" style="align-items:flex-start">${actions}</div>
      </div>
    </div>
    <div style="margin-bottom:14px"><button class="btn ghost small" data-act="back">${OM.icon('arrow-left', 16)} Все сообщества</button></div>`;
  }

  function composerHtml(c) {
    const admin = canManage(c);
    if (!c.is_member) {
      return '<div class="hint">Вступите в сообщество, чтобы публиковать.</div>';
    }
    if (c.kind === 'channel' && !admin) {
      return '<div class="hint">Это канал: публиковать могут только владелец и администраторы.</div>';
    }
    return `<div class="composer" id="comm-composer">
      <div class="field"><label>Заголовок</label>
        <input class="input" id="comm-post-title" maxlength="200" placeholder="Заголовок (необязательно)"></div>
      <div class="field"><label>Текст</label>
        <textarea id="comm-post-body" placeholder="Что нового в сообществе?"></textarea></div>
      <div class="row">
        <button class="btn ghost small" data-act="post-media">${OM.icon('attach', 16)} Изображение или видео</button>
        <span class="hint" id="comm-post-media-hint">файл не выбран</span>
        <button class="btn primary" data-act="post-send" style="margin-left:auto">Опубликовать</button>
      </div>
      <input type="file" id="comm-post-file" accept="image/*,video/*" class="hidden">
    </div>`;
  }

  function membersPanelHtml(c) {
    const admin = canManage(c);
    return `<div class="panel">
      <h3>Участники <span class="badge grey" id="comm-members-total">${OM.fmtNum(cur.members.length)}</span></h3>
      <div class="form-row" style="margin-top:10px">
        <input class="input" id="comm-member-search" placeholder="Фильтр по имени, @логину или Orange ID">
        ${admin && c.chat_id ? '<button class="btn primary" data-act="add-member" id="comm-add-member">Добавить участника</button>' : ''}
      </div>
      <div id="comm-member-list"></div>
    </div>`;
  }

  function renderDetail() {
    const box = $('community-detail');
    const c = cur.community;
    if (!box || !c) return;
    box.innerHTML = heroHtml(c) +
      `<div class="panel">
         <h3>Публикации сообщества</h3>
         <div class="desc">Публикации видны участникам сообщества.</div>
         ${composerHtml(c)}
         <div class="feed-list" id="comm-posts"></div>
         <button class="btn ghost wide hidden" id="comm-more" data-act="more-posts">Показать ещё</button>
       </div>` +
      membersPanelHtml(c);

    wireComposer();
    const filter = $('comm-member-search');
    if (filter) {
      filter.value = cur.memberFilter;
      filter.addEventListener('input', OM.debounce(() => {
        cur.memberFilter = filter.value.trim().toLowerCase();
        renderMembers();
      }, 150));
    }
    renderMembers();
    renderPosts();
  }

  function wireComposer() {
    const file = $('comm-post-file');
    if (!file) return;
    composerMedia = { url: '', kind: '', name: '' };
    file.onchange = async () => {
      const f = file.files && file.files[0];
      if (!f) return;
      const hint = $('comm-post-media-hint');
      hint.textContent = 'загрузка…';
      try {
        const up = await OM.api.upload(f, mediaKind(f));
        composerMedia = { url: up.url, kind: up.kind || mediaKind(f), name: f.name };
        hint.textContent = f.name;
      } catch (e) {
        hint.textContent = 'файл не выбран';
        OM.toast(e.message || 'Не удалось загрузить файл', 'err');
      }
    };
  }

  /* ======================================================================= ПУБЛИКАЦИИ */
  async function loadPosts(reset) {
    const c = cur.community;
    if (!c || cur.loading) return;
    cur.loading = true;
    try {
      let url = '/api/feed?scope=community&community=' + encodeURIComponent(c.id) + '&limit=20';
      if (!reset && cur.cursor) url += '&cursor=' + encodeURIComponent(cur.cursor);
      const r = await OM.api.get(url);
      const posts = r.posts || [];
      cur.posts = reset ? posts : cur.posts.concat(posts);
      cur.cursor = r.next_cursor || 0;
      renderPosts();
    } catch (e) {
      OM.toast(e.message || 'Не удалось загрузить публикации', 'err');
    }
    cur.loading = false;
  }

  function postHtml(p) {
    const author = p.author || {};
    const media = p.media
      ? (p.media_kind === 'video'
          ? `<div class="post-media"><video src="${esc(p.media)}" controls preload="metadata"></video></div>`
          : `<div class="post-media"><img src="${esc(p.media)}" alt="" loading="lazy"></div>`)
      : '';
    const tags = (p.tags && p.tags.length)
      ? `<div class="post-tags">${p.tags.map(t => `<span class="tag" data-tag="${esc(t)}">#${esc(t)}</span>`).join('')}</div>`
      : '';
    const del = p.is_mine ? '<button class="post-action" data-pact="del">Удалить</button>' : '';
    return `<article class="post-card" data-post="${p.id}">
      <div class="post-head">
        ${OM.avatar(author, { size: 'sm' })}
        <div class="who">
          <div class="nm">${esc(author.display_name || author.username || 'Автор')}</div>
          <div class="sub">
            <span>${esc(OM.timeAgo(p.created_at))}</span>
            ${p.edited_at ? '<span>изменено</span>' : ''}
          </div>
        </div>
      </div>
      ${p.title ? `<div class="post-title">${OM.linkify(p.title)}</div>` : ''}
      ${p.body ? `<div class="post-body">${OM.linkify(p.body)}</div>` : ''}
      ${media}${tags}
      <div class="post-actions">
        <button class="post-action${p.liked ? ' on' : ''}" data-pact="like">${OM.icon('heart', 16)} ${OM.fmtNum(p.likes)}</button>
        <button class="post-action" data-pact="comments">${OM.icon('comment', 16)} ${OM.fmtNum(p.comments)}</button>
        ${del}
      </div>
      <div class="comment-list hidden" data-comments="${p.id}"></div>
    </article>`;
  }

  function renderPosts() {
    const box = $('comm-posts');
    if (!box) return;
    box.innerHTML = cur.posts.length
      ? cur.posts.map(postHtml).join('')
      : `<div class="empty-state">
           <img src="/assets/logo.jpg" alt="" class="empty-logo">
           <p>Публикаций пока нет.</p>
         </div>`;
    const more = $('comm-more');
    if (more) more.classList.toggle('hidden', !cur.cursor);
  }

  async function createPost(btn) {
    const c = cur.community;
    if (!c) return;
    const titleI = $('comm-post-title');
    const bodyI = $('comm-post-body');
    const title = titleI ? titleI.value.trim() : '';
    const body = bodyI ? bodyI.value : '';
    if (!title && !body.trim() && !composerMedia.url) {
      OM.toast('Публикация не может быть пустой', 'err');
      return;
    }
    if (btn) btn.disabled = true;
    try {
      const payload = {
        community_id: c.id,
        title,
        body,
        media: composerMedia.url,
        media_kind: composerMedia.url ? composerMedia.kind : ''
      };
      const r = await OM.api.post('/api/posts', payload);
      if (titleI) titleI.value = '';
      if (bodyI) bodyI.value = '';
      const hint = $('comm-post-media-hint');
      if (hint) hint.textContent = 'файл не выбран';
      const file = $('comm-post-file');
      if (file) file.value = '';
      composerMedia = { url: '', kind: '', name: '' };
      if (r.post) cur.posts.unshift(r.post);
      renderPosts();
      OM.toast('Опубликовано', 'ok');
    } catch (e) {
      OM.toast(e.message || 'Не удалось опубликовать', 'err');
    }
    if (btn) btn.disabled = false;
  }

  async function toggleLike(postId) {
    const p = cur.posts.find(x => Number(x.id) === Number(postId));
    if (!p) return;
    try {
      const r = p.liked
        ? await OM.api.del('/api/posts/' + postId + '/like')
        : await OM.api.post('/api/posts/' + postId + '/like', {});
      p.liked = !!r.liked;
      if (r.likes !== undefined) p.likes = r.likes;
      const card = document.querySelector('#comm-posts .post-card[data-post="' + postId + '"]');
      if (card) {
        const b = card.querySelector('[data-pact="like"]');
        if (b) {
          b.classList.toggle('on', p.liked);
          b.innerHTML = OM.icon('heart', 16) + ' ' + OM.fmtNum(p.likes);
        }
      }
    } catch (e) {
      OM.toast(e.message || 'Не удалось поставить лайк', 'err');
    }
  }

  function commentHtml(cm) {
    const u = cm.user || {};
    return `<div class="comment">
      ${OM.avatar(u, { size: 'xs' })}
      <div class="cb">
        <div class="cn">${esc(u.display_name || u.username || 'Пользователь')}</div>
        <div class="ct">${OM.linkify(cm.body)}</div>
        <div class="cd">${esc(OM.timeAgo(cm.created_at))}</div>
      </div>
    </div>`;
  }

  async function loadComments(postId) {
    const box = document.querySelector('#comm-posts [data-comments="' + postId + '"]');
    if (!box) return;
    box.innerHTML = '<div class="hint">Загрузка комментариев…</div>';
    try {
      const r = await OM.api.get('/api/posts/' + postId + '/comments');
      const list = (r.comments || []).map(commentHtml).join('');
      box.innerHTML = (list || '<div class="hint">Комментариев пока нет.</div>') +
        `<div class="form-row" style="margin-top:10px">
           <input class="input" data-cinput="${postId}" placeholder="Написать комментарий…">
           <button class="btn primary small" data-cact="send" data-pid="${postId}">Отправить</button>
         </div>`;
    } catch (e) {
      box.innerHTML = `<div class="err">${esc(e.message || 'Не удалось загрузить комментарии')}</div>`;
    }
  }

  async function sendComment(postId) {
    const input = document.querySelector('#comm-posts [data-cinput="' + postId + '"]');
    if (!input) return;
    const body = input.value.trim();
    if (!body) return;
    input.disabled = true;
    try {
      const r = await OM.api.post('/api/posts/' + postId + '/comments', { body });
      const p = cur.posts.find(x => Number(x.id) === Number(postId));
      if (p) {
        p.comments = r.comments !== undefined ? r.comments : (Number(p.comments) || 0) + 1;
        const card = document.querySelector('#comm-posts .post-card[data-post="' + postId + '"]');
        const b = card && card.querySelector('[data-pact="comments"]');
        if (b) b.innerHTML = OM.icon('comment', 16) + ' ' + OM.fmtNum(p.comments);
      }
      await loadComments(postId);
    } catch (e) {
      OM.toast(e.message || 'Не удалось отправить комментарий', 'err');
      input.disabled = false;
    }
  }

  function deletePost(postId) {
    OM.confirm('Удалить публикацию? Действие необратимо.', async () => {
      try {
        await OM.api.del('/api/posts/' + postId);
        cur.posts = cur.posts.filter(x => Number(x.id) !== Number(postId));
        renderPosts();
        OM.toast('Публикация удалена', 'ok');
      } catch (e) {
        OM.toast(e.message || 'Не удалось удалить публикацию', 'err');
      }
    }, 'Удалить');
  }

  /* ======================================================================= УЧАСТНИКИ */
  function memberRowHtml(m) {
    const owner = isOwner(cur.community);
    const me = OM.state.me || {};
    let role = '<span class="badge grey">участник</span>';
    if (m.role === 'owner') role = '<span class="badge orange">владелец</span>';
    else if (m.role === 'admin') role = '<span class="badge">админ</span>';

    let actions = '';
    if (owner && m.role !== 'owner' && m.id !== me.id) {
      actions += m.role === 'admin'
        ? `<button class="btn small ghost" data-mact="member" data-uid="${m.id}">Снять админа</button>`
        : `<button class="btn small ghost" data-mact="admin" data-uid="${m.id}">Сделать админом</button>`;
      actions += `<button class="btn small danger" data-mact="kick" data-uid="${m.id}">Исключить</button>`;
    }
    return `<div class="member-row">
      ${OM.avatar(m, { size: 'sm', online: !!(m.presence && m.presence.online) })}
      <div class="cw" style="flex:1;min-width:0">
        <div class="comm-name" style="font-size:14px">${esc(m.display_name || m.username)} ${role}</div>
        <div class="hint">@${esc(m.username)} · ${esc(m.orange_id || '')}${m.short_id ? ' · короткий ID ' + esc(m.short_id) : ''}</div>
      </div>
      ${actions}
    </div>`;
  }

  function renderMembers() {
    const box = $('comm-member-list');
    if (!box) return;
    const f = cur.memberFilter;
    const list = f
      ? cur.members.filter(m => {
          const hay = [m.display_name, m.username, m.orange_id, m.short_id].join(' ').toLowerCase();
          return hay.indexOf(f) >= 0;
        })
      : cur.members;
    box.innerHTML = list.length
      ? list.map(memberRowHtml).join('')
      : '<div class="hint">Никого не найдено.</div>';
    const total = $('comm-members-total');
    if (total) total.textContent = OM.fmtNum(cur.members.length);
    const counter = $('comm-members-count');
    if (counter && cur.community) counter.textContent = OM.fmtNum(cur.community.members);
  }

  async function memberAction(act, uid) {
    const c = cur.community;
    if (!c) return;
    const m = cur.members.find(x => Number(x.id) === Number(uid));
    try {
      if (act === 'admin' || act === 'member') {
        await OM.api.post('/api/communities/' + c.id + '/members/' + uid + '/role', { role: act });
        if (m) m.role = act;
        renderMembers();
        OM.toast(act === 'admin' ? 'Назначен администратором' : 'Права администратора сняты', 'ok');
      }
    } catch (e) {
      OM.toast(e.message || 'Не удалось изменить роль', 'err');
    }
  }

  function kickMember(uid) {
    const c = cur.community;
    if (!c) return;
    const m = cur.members.find(x => Number(x.id) === Number(uid));
    const name = m ? (m.display_name || m.username) : 'участника';
    OM.confirm('Исключить ' + name + ' из сообщества?', async () => {
      try {
        await OM.api.del('/api/communities/' + c.id + '/members/' + uid);
        cur.members = cur.members.filter(x => Number(x.id) !== Number(uid));
        if (cur.community.members > 0) cur.community.members--;
        renderMembers();
        OM.toast('Участник исключён', 'ok');
      } catch (e) {
        OM.toast(e.message || 'Не удалось исключить участника', 'err');
      }
    }, 'Исключить');
  }

  /** Добавление участника возможно только через чат сообщества */
  function openAddMember() {
    const c = cur.community;
    if (!c) return;
    if (!c.chat_id) { OM.toast('У сообщества пока нет чата', 'err'); return; }
    const m = OM.modal({
      title: 'Добавить участника',
      body: `
        <div class="field"><label>Поиск пользователя</label>
          <input class="input" id="am-q" placeholder="Логин, e-mail или Orange ID"></div>
        <div class="hint">Пользователь добавляется в чат сообщества и сразу получает доступ к обсуждению.</div>
        <div id="am-results" style="margin-top:10px"><div class="hint">Введите минимум 2 символа…</div></div>`,
      actions: [{ label: 'Закрыть', cls: 'ghost' }]
    });
    const q = m.body.querySelector('#am-q');
    const results = m.body.querySelector('#am-results');

    const search = OM.debounce(async () => {
      const val = q.value.trim();
      if (val.length < 2) { results.innerHTML = '<div class="hint">Введите минимум 2 символа…</div>'; return; }
      results.innerHTML = '<div class="hint">Поиск…</div>';
      try {
        const r = await OM.api.get('/api/users/search?q=' + encodeURIComponent(val));
        const meId = OM.state.me ? OM.state.me.id : 0;
        const users = (r.users || []).filter(u => u.id !== meId);
        results.innerHTML = users.length
          ? users.map(u => `<div class="member-row">
              ${OM.avatar(u, { size: 'sm' })}
              <div class="cw" style="flex:1;min-width:0">
                <div class="comm-name" style="font-size:14px">${esc(u.display_name || u.username)}</div>
                <div class="hint">@${esc(u.username)} · ${esc(u.orange_id || '')}</div>
              </div>
              <button class="btn small primary" data-add="${u.id}" data-ident="${esc(u.username)}">Добавить</button>
            </div>`).join('')
          : '<div class="hint">Никого не найдено.</div>';
      } catch (e) {
        results.innerHTML = `<div class="err">${esc(e.message || 'Ошибка поиска')}</div>`;
      }
    }, 400);

    q.addEventListener('input', search);
    results.addEventListener('click', async e => {
      const b = e.target.closest('[data-add]');
      if (!b) return;
      b.disabled = true;
      try {
        await OM.api.post('/api/chats/' + c.chat_id + '/members', { identifier: b.dataset.ident });
        b.textContent = 'Добавлен';
        OM.toast('Участник добавлен', 'ok');
        const r = await OM.api.get('/api/communities/' + c.id);
        cur.members = r.members || cur.members;
        cur.community = r.community || cur.community;
        renderMembers();
      } catch (err) {
        b.disabled = false;
        OM.toast(err.message || 'Не удалось добавить участника', 'err');
      }
    });
    setTimeout(() => q.focus(), 60);
  }

  /* ======================================================================= ДЕЙСТВИЯ СТРАНИЦЫ */
  async function joinCommunity() {
    const c = cur.community;
    if (!c) return;
    try {
      await OM.api.post('/api/communities/' + c.id + '/join', {});
      OM.toast('Вы вступили в сообщество', 'ok');
      await loadCommunity(c.id);
    } catch (e) {
      OM.toast(e.message || 'Не удалось вступить', 'err');
    }
  }

  function leaveCommunity() {
    const c = cur.community;
    if (!c) return;
    OM.confirm('Покинуть сообщество «' + c.name + '»?', async () => {
      try {
        await OM.api.post('/api/communities/' + c.id + '/leave', {});
        OM.toast('Вы покинули сообщество', 'ok');
        await loadCommunity(c.id);
      } catch (e) {
        OM.toast(e.message || 'Не удалось покинуть сообщество', 'err');
      }
    }, 'Покинуть');
  }

  async function uploadCommunityImage(kind) {
    const c = cur.community;
    if (!c) return;
    const file = await pickFile('image/*');
    if (!file) return;
    try {
      const up = await OM.api.upload(file, 'image');
      const body = {};
      body[kind] = up.url;
      const r = await OM.api.patch('/api/communities/' + c.id, body);
      cur.community = r.community || cur.community;
      renderDetail();
      OM.toast(kind === 'banner' ? 'Баннер обновлён' : 'Аватар обновлён', 'ok');
    } catch (e) {
      OM.toast(e.message || 'Не удалось обновить изображение', 'err');
    }
  }

  async function createInvite() {
    const c = cur.community;
    if (!c) return;
    try {
      const r = await OM.api.post('/api/communities/' + c.id + '/invites', {});
      const link = r.link || (location.origin + '/join/' + r.code);
      OM.modal({
        title: 'Приглашение в сообщество',
        body: `
          <div class="field"><label>Код</label><div class="secret">${esc(r.code)}</div></div>
          <div class="field"><label>Ссылка</label>
            <input class="input" readonly value="${esc(link)}"></div>
          <div class="hint">Отправьте код или ссылку — по ним можно вступить в сообщество.</div>`,
        actions: [
          { label: 'Скопировать код', cls: 'ghost', onClick: () => OM.copy(r.code) },
          { label: 'Скопировать ссылку', cls: 'primary', onClick: () => OM.copy(link) }
        ]
      });
    } catch (e) {
      OM.toast(e.message || 'Не удалось создать приглашение', 'err');
    }
  }

  function openSettings() {
    const c = cur.community;
    if (!c) return;
    const m = OM.modal({
      title: 'Настройки сообщества',
      body: `
        <div class="field"><label>Название</label>
          <input class="input" id="cs-name" maxlength="80" value="${esc(c.name)}"></div>
        <div class="field"><label>Описание</label>
          <textarea id="cs-desc" maxlength="500">${esc(c.description || '')}</textarea></div>
        <div class="field"><label>Тип</label>
          <select id="cs-kind">
            <option value="group"${c.kind !== 'channel' ? ' selected' : ''}>Группа</option>
            <option value="channel"${c.kind === 'channel' ? ' selected' : ''}>Канал</option>
          </select></div>
        <div class="switch"><div class="lb">Открытое сообщество<small>можно вступить без приглашения</small></div>
          <button class="toggle${c.is_public ? ' on' : ''}" id="cs-public"></button></div>
        <div class="err hidden" id="cs-err"></div>`,
      actions: [
        { label: 'Отмена', cls: 'ghost' },
        {
          label: 'Сохранить', cls: 'primary',
          onClick: async modal => {
            const err = m.body.querySelector('#cs-err');
            err.classList.add('hidden');
            try {
              const r = await OM.api.patch('/api/communities/' + c.id, {
                name: m.body.querySelector('#cs-name').value.trim(),
                description: m.body.querySelector('#cs-desc').value,
                kind: m.body.querySelector('#cs-kind').value,
                is_public: m.body.querySelector('#cs-public').classList.contains('on')
              });
              cur.community = r.community || cur.community;
              modal.close();
              renderDetail();
              OM.toast('Настройки сохранены', 'ok');
            } catch (e) {
              err.textContent = e.message || 'Не удалось сохранить';
              err.classList.remove('hidden');
            }
          }
        }
      ]
    });
    const pub = m.body.querySelector('#cs-public');
    pub.onclick = () => pub.classList.toggle('on');
  }

  function deleteCommunity() {
    const c = cur.community;
    if (!c) return;
    OM.confirm('Удалить сообщество «' + c.name + '»? Все публикации и чат будут скрыты.', async () => {
      try {
        await OM.api.del('/api/communities/' + c.id);
        OM.toast('Сообщество удалено', 'ok');
        cur.community = null;
        goBack();
      } catch (e) {
        OM.toast(e.message || 'Не удалось удалить сообщество', 'err');
      }
    }, 'Удалить');
  }

  /** Возврат к списку сообществ */
  function goBack() {
    OM.navigate('communities');
  }

  /** Общий делегированный обработчик кликов по странице сообщества */
  function bindDetail() {
    if (cur.bound) return;
    cur.bound = true;
    const box = $('community-detail');
    if (!box) return;

    box.addEventListener('click', e => {
      const act = e.target.closest('[data-act]');
      if (act) {
        const a = act.dataset.act;
        if (a === 'back') { goBack(); return; }
        if (a === 'join') { joinCommunity(); return; }
        if (a === 'leave') { leaveCommunity(); return; }
        if (a === 'chat') { openChat(cur.community && cur.community.chat_id); return; }
        if (a === 'invite') { createInvite(); return; }
        if (a === 'settings') { openSettings(); return; }
        if (a === 'delete') { deleteCommunity(); return; }
        if (a === 'avatar') { uploadCommunityImage('avatar'); return; }
        if (a === 'banner') { uploadCommunityImage('banner'); return; }
        if (a === 'add-member') { openAddMember(); return; }
        if (a === 'more-posts') { loadPosts(false); return; }
        if (a === 'post-send') { createPost(act); return; }
        if (a === 'post-media') {
          const f = $('comm-post-file');
          if (f) f.click();
          return;
        }
      }

      const mact = e.target.closest('[data-mact]');
      if (mact) {
        const uid = mact.dataset.uid;
        if (mact.dataset.mact === 'kick') kickMember(uid);
        else memberAction(mact.dataset.mact, uid);
        return;
      }

      const pact = e.target.closest('[data-pact]');
      if (pact) {
        const card = pact.closest('.post-card');
        const pid = card && card.dataset.post;
        if (!pid) return;
        if (pact.dataset.pact === 'like') { toggleLike(pid); return; }
        if (pact.dataset.pact === 'del') { deletePost(pid); return; }
        if (pact.dataset.pact === 'comments') {
          const cbox = card.querySelector('[data-comments]');
          if (!cbox) return;
          if (!cbox.classList.contains('hidden')) { cbox.classList.add('hidden'); return; }
          loadComments(pid).then(() => cbox.classList.remove('hidden'));
          return;
        }
      }

      const cact = e.target.closest('[data-cact="send"]');
      if (cact) { sendComment(cact.dataset.pid); return; }
    });

    // Enter в поле комментария отправляет его
    box.addEventListener('keydown', e => {
      if (e.key !== 'Enter') return;
      const input = e.target.closest('[data-cinput]');
      if (!input) return;
      e.preventDefault();
      sendComment(input.dataset.cinput);
    });
  }

  /* ======================================================================= РЕАЛТАЙМ */
  OM.on('ws:post.new', d => {
    const p = d && d.post;
    if (!p || !cur.community) return;
    const cid = p.community_id || (p.community && p.community.id);
    if (Number(cid) !== Number(cur.community.id)) return;
    if (cur.posts.some(x => Number(x.id) === Number(p.id))) return;
    cur.posts.unshift(p);
    renderPosts();
    OM.toast('Новая публикация в сообществе', 'ok');
  });

  /* ======================================================================= РЕГИСТРАЦИЯ РАЗДЕЛОВ */
  OM.openCommunity = function (id) {
    OM.navigate('community', { id });
  };

  OM.on('open-community', id => {
    if (id) OM.openCommunity(id);
  });

  OM.views.communities = {
    enter() {
      bindList();
      syncTabs();
      maybeJoinFromPath();
      loadList();
    },
    leave() {
      listState.reqId++; // отменяем устаревшие ответы
    }
  };

  OM.views.community = {
    async enter(params) {
      params = params || {};
      const key = params.id !== undefined && params.id !== null && params.id !== ''
        ? params.id
        : cur.key;
      cur.key = key;
      cur.memberFilter = '';
      bindDetail();
      if (!key) {
        const box = $('community-detail');
        if (box) box.innerHTML = '<div class="panel"><h3>Сообщество не выбрано</h3><div class="desc">Откройте список сообществ и выберите сообщество.</div><button class="btn ghost" data-act="back">' + OM.icon('arrow-left', 16) + ' Все сообщества</button></div>';
        return;
      }
      await loadCommunity(key);
    },
    leave() {
      cur.reqId++; // отменяем устаревшие ответы
    }
  };
})();
