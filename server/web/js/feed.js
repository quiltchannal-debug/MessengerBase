/* OrangeM — лента: посты, тренды, композер, комментарии, поиск, realtime
   Раздел: #view-feed (#feed-list, #feed-trends, #feed-composer, #feed-tabs,
   #feed-search, #feed-search-btn, #post-new-btn, #feed-more) */
window.OM = window.OM || {};

(function () {
  'use strict';

  const byId = id => document.getElementById(id);

  /* ------------------------------------------------------------------ состояние раздела */
  const F = {
    cursor: null,       // курсор следующей страницы
    hasMore: false,     // есть ли ещё страницы
    loading: false,     // идёт загрузка
    seq: 0,             // счётчик запросов (защита от гонки)
    alive: false,       // раздел открыт
    observer: null,     // IntersectionObserver на кнопке «Показать ещё»
    pending: [],        // новые публикации из WS (null = неизвестно, нужен reload)
    inited: false       // обработчики навешаны
  };

  /* Черновик композера */
  const draft = { media: '', media_kind: '' };

  /* ------------------------------------------------------------------ утилиты */

  function searchActive() {
    return !!(OM.state.feedQuery || OM.state.feedTag);
  }

  /** Извлекает хештеги из текста, если сервер не прислал tags */
  function extractTags(text) {
    const m = String(text || '').match(/#[\wа-яёА-ЯЁ_]{2,}/g) || [];
    return m.map(t => t.slice(1));
  }

  function visibilityLabel(v) {
    if (v === 'followers') return 'Подписчикам';
    if (v === 'public') return 'Всем';
    return v ? String(v) : 'Всем';
  }

  /* ------------------------------------------------------------------ отрисовка поста */

  function postHtml(post) {
    const a = post.author || {};
    const comm = post.community || null;
    const tags = (post.tags && post.tags.length ? post.tags : extractTags(post.body)).slice(0, 12);
    const name = a.display_name || a.username || 'Пользователь';

    const media = post.media
      ? (post.media_kind === 'video'
        ? `<div class="post-media"><video src="${OM.esc(post.media)}" controls preload="metadata"></video></div>`
        : `<div class="post-media"><img src="${OM.esc(post.media)}" alt="" loading="lazy"></div>`)
      : '';

    const badge = comm
      ? `<span class="badge orange" data-act="community" data-cid="${OM.esc(comm.id)}" title="Сообщество">${OM.esc(comm.name || 'Сообщество')}</span>`
      : '';

    const tagList = tags.length
      ? `<div class="post-tags">${tags.map(t => `<span class="tag" data-tag="${OM.esc(t)}">#${OM.esc(t)}</span>`).join('')}</div>`
      : '';

    const del = post.is_mine
      ? '<button class="post-action" data-act="del" title="Удалить">' + OM.icon('trash', 16) + ' удалить</button>'
      : '';

    return `<article class="post-card" data-id="${OM.esc(post.id)}">
      <div class="post-head">
        <span data-act="profile" data-uid="${OM.esc(a.id || '')}" title="${OM.esc(name)}">${OM.avatar(a, { size: 'sm' })}</span>
        <div class="who">
          <div class="nm"><span data-act="profile" data-uid="${OM.esc(a.id || '')}">${OM.esc(name)}</span>${badge}</div>
          <div class="sub">
            <span title="${OM.esc(OM.fmtDateTime(post.created_at))}">${OM.esc(OM.timeAgo(post.created_at))}</span>
            <span>${OM.esc(visibilityLabel(post.visibility))}</span>
            ${post.edited_at ? '<span>изменено</span>' : ''}
          </div>
        </div>
      </div>
      ${post.title ? `<div class="post-title">${OM.linkify(post.title)}</div>` : ''}
      ${post.body ? `<div class="post-body">${OM.linkify(post.body)}</div>` : ''}
      ${media}
      ${tagList}
      <div class="post-actions">
        <button class="post-action ${post.liked ? 'on' : ''}" data-act="like" title="Нравится">${OM.icon('heart', 16)} <span class="cnt">${OM.fmtNum(post.likes || 0)}</span></button>
        <button class="post-action" data-act="comments" title="Комментарии">${OM.icon('comment', 16)} <span class="cnt" data-role="ccount">${OM.fmtNum(post.comments || 0)}</span></button>
        <button class="post-action" data-act="copy" title="Скопировать ссылку">${OM.icon('link', 16)} ссылка</button>
        ${del}
      </div>
      <div class="comment-list hidden" data-role="comments" data-loaded="0"></div>
    </article>`;
  }

  function emptyHtml() {
    const q = OM.state.feedTag ? '#' + OM.state.feedTag : OM.state.feedQuery;
    const text = q
      ? `Ничего не найдено по запросу «${OM.esc(q)}»`
      : (OM.state.feedScope === 'following'
        ? 'В подписках пока нет публикаций'
        : (OM.state.feedScope === 'mine' ? 'Вы ещё ничего не публиковали' : 'Пока нет публикаций'));
    return `<div class="empty-state">
      <img src="/assets/logo.jpg" class="empty-logo" alt="">
      <p>${text}</p>
      ${searchActive() ? '<button class="btn ghost" data-act="reset-search">Сбросить поиск</button>' : ''}
    </div>`;
  }

  /* ------------------------------------------------------------------ загрузка ленты */

  function feedUrl(cursor) {
    const parts = [
      'scope=' + encodeURIComponent(OM.state.feedScope || 'global'),
      'limit=20'
    ];
    if (cursor) parts.push('cursor=' + encodeURIComponent(cursor));
    if (OM.state.feedQuery) parts.push('q=' + encodeURIComponent(OM.state.feedQuery));
    if (OM.state.feedTag) parts.push('tag=' + encodeURIComponent(OM.state.feedTag));
    return '/api/feed?' + parts.join('&');
  }

  async function load(reset) {
    if (!F.alive) return;
    const list = byId('feed-list');
    if (!list) return;
    if (!reset && (F.loading || !F.hasMore)) return;
    if (reset) {
      F.cursor = null;
      F.hasMore = true;
      list.innerHTML = '<div class="hint">Загрузка ленты…</div>';
      /* при смене раздела/поиска «отложенные» публикации больше не актуальны */
      const pending = byId('feed-new-posts');
      if (pending) pending.remove();
      F.pending = [];
    }
    const my = ++F.seq;
    F.loading = true;
    try {
      const r = await OM.api.get(feedUrl(reset ? null : F.cursor));
      if (my !== F.seq || !F.alive) return;
      const posts = r.posts || [];
      if (reset) list.innerHTML = '';
      const hint = list.querySelector('.hint');
      if (hint && !list.querySelector('.post-card')) hint.remove();
      if (!posts.length && reset) list.innerHTML = emptyHtml();
      else posts.forEach(p => list.insertAdjacentHTML('beforeend', postHtml(p)));
      F.cursor = r.next_cursor || null;
      F.hasMore = !!F.cursor;
      syncMore();
    } catch (e) {
      if (my !== F.seq) return;
      F.hasMore = false;
      syncMore();
      list.innerHTML = `<div class="empty-state">
        <img src="/assets/logo.jpg" class="empty-logo" alt="">
        <p>${OM.esc(e.message || 'Не удалось загрузить ленту')}</p>
        <button class="btn ghost" data-act="reload">Повторить</button>
      </div>`;
    } finally {
      if (my === F.seq) {
        F.loading = false;
        maybeAutoLoad();
      }
    }
  }

  /** Страховка для бесконечной прокрутки: если кнопка всё ещё видна — грузим дальше */
  function maybeAutoLoad() {
    if (!F.alive || F.loading || !F.hasMore) return;
    const more = byId('feed-more');
    if (!more) return;
    const r = more.getBoundingClientRect();
    if (r.top < (window.innerHeight || 800) + 400) setTimeout(() => load(false), 80);
  }

  function syncMore() {
    const more = byId('feed-more');
    if (!more) return;
    more.classList.toggle('hidden', !F.hasMore);
    if (F.hasMore) attachObserver();
  }

  /** Бесконечная прокрутка: наблюдаем за кнопкой #feed-more */
  function attachObserver() {
    const more = byId('feed-more');
    if (!more || F.observer || typeof IntersectionObserver !== 'function') return;
    F.observer = new IntersectionObserver(entries => {
      if (entries.some(e => e.isIntersecting)) load(false);
    }, { rootMargin: '400px 0px' });
    F.observer.observe(more);
  }

  /* ------------------------------------------------------------------ тренды */

  async function loadTrends() {
    const box = byId('feed-trends');
    if (!box) return;
    try {
      const r = await OM.api.get('/api/feed/trends');
      const list = r.trends || [];
      box.innerHTML = list.length
        ? list.map(t => `<button class="trend-pill" data-tag="${OM.esc(t.tag)}">#${OM.esc(t.tag)} <span class="badge grey">${OM.fmtNum(t.posts || 0)}</span></button>`).join('')
        : '';
    } catch (e) {
      box.innerHTML = '';
    }
  }

  /* ------------------------------------------------------------------ поиск и вкладки */

  function syncTabs() {
    const tabs = byId('feed-tabs');
    if (!tabs) return;
    tabs.querySelectorAll('.chip[data-scope]').forEach(c => {
      c.classList.toggle('active', c.dataset.scope === OM.state.feedScope);
    });
    let chip = tabs.querySelector('.chip[data-reset]');
    if (!searchActive()) { if (chip) chip.remove(); return; }
    if (!chip) {
      chip = document.createElement('button');
      chip.className = 'chip';
      chip.dataset.reset = '1';
      tabs.appendChild(chip);
    }
    chip.innerHTML = OM.icon('close', 14) + ' ' +
      OM.esc('Сбросить: ' + (OM.state.feedTag ? '#' + OM.state.feedTag : OM.state.feedQuery));
  }

  function doSearch() {
    const inp = byId('feed-search');
    const raw = ((inp && inp.value) || '').trim();
    if (raw.charAt(0) === '#') {
      OM.state.feedTag = raw.slice(1).trim();
      OM.state.feedQuery = '';
    } else {
      OM.state.feedQuery = raw;
      OM.state.feedTag = '';
    }
    syncTabs();
    load(true);
  }

  function startTagSearch(tag) {
    const t = String(tag || '').replace(/^#/, '').trim();
    if (!t) return;
    OM.state.feedTag = t;
    OM.state.feedQuery = '';
    const inp = byId('feed-search');
    if (inp) inp.value = '#' + t;
    syncTabs();
    load(true);
  }

  function resetSearch() {
    OM.state.feedQuery = '';
    OM.state.feedTag = '';
    const inp = byId('feed-search');
    if (inp) inp.value = '';
    syncTabs();
    load(true);
  }

  async function openMention(username) {
    try {
      const r = await OM.api.get('/api/users/' + encodeURIComponent(username));
      const u = r.user || r;
      if (u && u.id) OM.navigate('profile', { id: u.id });
    } catch (e) {
      OM.toast('Пользователь не найден', 'err');
    }
  }

  /* ------------------------------------------------------------------ композер */

  function resetDraft() {
    draft.media = '';
    draft.media_kind = '';
    const t = byId('pf-title'); if (t) t.value = '';
    const b = byId('pf-body'); if (b) b.value = '';
    const f = byId('pf-file'); if (f) f.value = '';
    const p = byId('pf-preview'); if (p) p.innerHTML = '';
    const v = byId('pf-vis'); if (v) v.value = 'public';
  }

  function toggleComposer(force) {
    const box = byId('feed-composer');
    if (!box) return;
    buildComposer();
    const show = force === undefined ? box.classList.contains('hidden') : !!force;
    box.classList.toggle('hidden', !show);
    if (show) {
      const t = byId('pf-title');
      if (t) t.focus();
    } else {
      resetDraft();
    }
  }

  function buildComposer() {
    const box = byId('feed-composer');
    if (!box || box.dataset.built === '1') return;
    box.dataset.built = '1';
    box.innerHTML = `
      <div class="field"><label>Заголовок</label>
        <input class="input" id="pf-title" maxlength="200" placeholder="О чём публикация?"></div>
      <div class="field"><label>Текст</label>
        <textarea id="pf-body" maxlength="5000" placeholder="Напишите что-нибудь… #хештеги, @упоминания и ссылки поддерживаются"></textarea></div>
      <div class="form-row">
        <div class="field"><label>Кто видит</label>
          <select id="pf-vis">
            <option value="public">Все</option>
            <option value="followers">Подписчики</option>
          </select></div>
        <div class="field"><label>Фото или видео</label>
          <input type="file" id="pf-file" accept="image/*,video/*">
          <div class="hint">Файл загрузится на сервер OrangeM.</div></div>
      </div>
      <div id="pf-preview"></div>
      <div class="hint">Подсказка: <b>#хештег</b> попадёт в тренды, <b>@упоминание</b> отметит пользователя.</div>
      <div class="row">
        <button class="btn primary" id="pf-send">Опубликовать</button>
        <button class="btn ghost" id="pf-cancel">Отмена</button>
      </div>`;
    byId('pf-file').addEventListener('change', onPickFile);
    byId('pf-send').addEventListener('click', publish);
    byId('pf-cancel').addEventListener('click', () => toggleComposer(false));
  }

  async function onPickFile(e) {
    const prev = byId('pf-preview');
    const f = e.target.files && e.target.files[0];
    if (!f) { draft.media = ''; draft.media_kind = ''; if (prev) prev.innerHTML = ''; return; }
    const kind = /^video\//.test(f.type || '') ? 'video' : 'image';
    if (prev) prev.innerHTML = '<div class="hint">Загрузка файла…</div>';
    try {
      const up = await OM.api.upload(f, kind);
      draft.media = up.url || '';
      draft.media_kind = up.kind || kind;
      if (prev) {
        prev.innerHTML = draft.media_kind === 'video'
          ? `<div class="post-media"><video src="${OM.esc(draft.media)}" controls></video></div>`
          : `<div class="post-media"><img src="${OM.esc(draft.media)}" alt=""></div>`;
      }
    } catch (err) {
      draft.media = '';
      draft.media_kind = '';
      if (prev) prev.innerHTML = '';
      OM.toast(err.message || 'Не удалось загрузить файл', 'err');
    }
  }

  function prependPost(post) {
    const list = byId('feed-list');
    if (!list) return;
    const em = list.querySelector('.empty-state');
    if (em) em.remove();
    /* если сверху висит кнопка «новые публикации» — вставляем выше неё */
    const btn = byId('feed-new-posts');
    if (btn) btn.insertAdjacentHTML('beforebegin', postHtml(post));
    else list.insertAdjacentHTML('afterbegin', postHtml(post));
  }

  async function publish() {
    const title = (byId('pf-title').value || '').trim();
    const body = (byId('pf-body').value || '').trim();
    const vis = byId('pf-vis').value || 'public';
    if (!title && !body && !draft.media) { OM.toast('Добавьте текст или вложение', 'err'); return; }
    const btn = byId('pf-send');
    btn.disabled = true;
    try {
      const payload = { title, body, visibility: vis };
      if (draft.media) { payload.media = draft.media; payload.media_kind = draft.media_kind || 'image'; }
      const r = await OM.api.post('/api/posts', payload);
      const post = r.post;
      OM.toast('Опубликовано', 'ok');
      OM.emit('post-created', post);
      toggleComposer(false);
      if (post) {
        if (OM.state.feedScope === 'mine' || OM.state.feedScope === 'global' || OM.state.feedScope === 'following') {
          prependPost(post);
        }
      }
      loadTrends();
    } catch (e) {
      OM.toast(e.message || 'Не удалось опубликовать', 'err');
    }
    btn.disabled = false;
  }

  /* ------------------------------------------------------------------ комментарии */

  function renderComments(box, comments) {
    const rows = comments.map(c => {
      const u = c.user || {};
      const delBtn = c.is_mine
        ? ` · <button class="post-action" data-act="del-comment" data-cid="${OM.esc(c.id)}">удалить</button>`
        : '';
      return `<div class="comment" data-cid="${OM.esc(c.id)}">
        <span data-act="profile" data-uid="${OM.esc(u.id || '')}">${OM.avatar(u, { size: 'xs' })}</span>
        <div class="cb">
          <div class="cn">${OM.esc(u.display_name || u.username || 'Пользователь')}</div>
          <div class="ct">${OM.esc(c.body || '')}</div>
          <div class="cd">${OM.esc(OM.timeAgo(c.created_at))}${delBtn}</div>
        </div>
      </div>`;
    }).join('');
    box.innerHTML = rows + `<div class="form-row">
        <input class="input" data-role="comment-input" maxlength="1000" placeholder="Написать комментарий…">
        <button class="btn primary small" data-act="send-comment">Отправить</button>
      </div>`;
    box.dataset.loaded = '1';
  }

  async function toggleComments(card) {
    const box = card.querySelector('[data-role="comments"]');
    if (!box) return;
    if (!box.classList.contains('hidden')) { box.classList.add('hidden'); return; }
    box.classList.remove('hidden');
    if (box.dataset.loaded === '1') return;
    const id = card.dataset.id;
    box.innerHTML = '<div class="hint">Загрузка комментариев…</div>';
    try {
      const r = await OM.api.get('/api/posts/' + encodeURIComponent(id) + '/comments');
      renderComments(box, r.comments || []);
    } catch (e) {
      box.innerHTML = `<div class="hint">${OM.esc(e.message || 'Не удалось загрузить комментарии')}</div>`;
    }
  }

  async function sendComment(card) {
    const box = card.querySelector('[data-role="comments"]');
    if (!box) return;
    const inp = box.querySelector('[data-role="comment-input"]');
    const body = ((inp && inp.value) || '').trim();
    if (!body) return;
    const id = card.dataset.id;
    try {
      // POST возвращает {comment_id, comments: <число>} — список перезапрашиваем целиком,
      // чтобы получить объекты комментариев (а не счётчик).
      await OM.api.post('/api/posts/' + encodeURIComponent(id) + '/comments', { body });
      const r = await OM.api.get('/api/posts/' + encodeURIComponent(id) + '/comments');
      const list = Array.isArray(r.comments) ? r.comments : [];
      renderComments(box, list);
      const cc = card.querySelector('[data-role="ccount"]');
      if (cc) cc.textContent = OM.fmtNum(list.length);
      if (inp) inp.value = '';
    } catch (e) {
      OM.toast(e.message || 'Не удалось отправить комментарий', 'err');
    }
  }

  async function deleteComment(card, cid) {
    try {
      await OM.api.del('/api/comments/' + encodeURIComponent(cid));
      const box = card.querySelector('[data-role="comments"]');
      if (box) {
        const r = await OM.api.get('/api/posts/' + encodeURIComponent(card.dataset.id) + '/comments');
        renderComments(box, r.comments || []);
        const cc = card.querySelector('[data-role="ccount"]');
        if (cc) cc.textContent = OM.fmtNum((r.comments || []).length);
      }
      OM.toast('Комментарий удалён', 'ok');
    } catch (e) {
      OM.toast(e.message || 'Ошибка', 'err');
    }
  }

  /* ------------------------------------------------------------------ действия над постом */

  async function toggleLike(card) {
    const btn = card.querySelector('[data-act="like"]');
    if (!btn) return;
    const id = card.dataset.id;
    const on = btn.classList.contains('on');
    btn.disabled = true;
    try {
      const r = on
        ? await OM.api.del('/api/posts/' + encodeURIComponent(id) + '/like')
        : await OM.api.post('/api/posts/' + encodeURIComponent(id) + '/like');
      btn.classList.toggle('on', !!r.liked);
      const c = btn.querySelector('.cnt');
      if (c) c.textContent = OM.fmtNum(r.likes != null ? r.likes : 0);
    } catch (e) {
      OM.toast(e.message || 'Ошибка', 'err');
    }
    btn.disabled = false;
  }

  function deletePost(card) {
    const id = card.dataset.id;
    OM.confirm('Удалить публикацию? Это действие необратимо.', async () => {
      try {
        await OM.api.del('/api/posts/' + encodeURIComponent(id));
        card.remove();
        OM.toast('Публикация удалена', 'ok');
      } catch (e) {
        OM.toast(e.message || 'Не удалось удалить', 'err');
      }
    }, 'Удалить');
  }

  /* ------------------------------------------------------------------ делегирование событий */

  function onListClick(e) {
    /* ссылки внутри текста поста — перехватываем до всего остального */
    const omTag = e.target.closest('.om-tag');
    if (omTag && byId('feed-list').contains(omTag)) {
      e.preventDefault();
      startTagSearch(omTag.dataset.tag);
      return;
    }
    const omMention = e.target.closest('.om-mention');
    if (omMention && byId('feed-list').contains(omMention)) {
      e.preventDefault();
      openMention(omMention.dataset.user);
      return;
    }
    const tagEl = e.target.closest('.tag[data-tag]');
    if (tagEl) { startTagSearch(tagEl.dataset.tag); return; }

    const plain = e.target.closest('[data-act="reset-search"],[data-act="reload"]');
    if (plain) {
      if (plain.dataset.act === 'reset-search') resetSearch();
      else load(true);
      return;
    }

    const card = e.target.closest('.post-card');
    if (!card) return;
    const btn = e.target.closest('[data-act]');
    if (!btn) return;
    const act = btn.dataset.act;

    if (act === 'profile') {
      const uid = btn.dataset.uid;
      if (uid) OM.navigate('profile', { id: Number(uid) || uid });
    } else if (act === 'community') {
      if (btn.dataset.cid) OM.navigate('community', { id: btn.dataset.cid });
    } else if (act === 'like') {
      toggleLike(card);
    } else if (act === 'comments') {
      toggleComments(card);
    } else if (act === 'copy') {
      OM.copy(location.origin + '/#post' + card.dataset.id);
    } else if (act === 'del') {
      deletePost(card);
    } else if (act === 'send-comment') {
      sendComment(card);
    } else if (act === 'del-comment') {
      deleteComment(card, btn.dataset.cid);
    }
  }

  function onListKeydown(e) {
    if (e.key !== 'Enter') return;
    const t = e.target;
    if (!t || t.getAttribute('data-role') !== 'comment-input') return;
    e.preventDefault();
    const card = t.closest('.post-card');
    if (card) sendComment(card);
  }

  /* ------------------------------------------------------------------ realtime: новые публикации */

  function noteNew(post) {
    if (!F.alive) return;
    const scope = OM.state.feedScope;
    if (scope !== 'global' && scope !== 'following') return;
    if (searchActive()) return;
    F.pending.push(post || null);
    showPendingButton();
  }

  function showPendingButton() {
    const list = byId('feed-list');
    if (!list) return;
    let b = byId('feed-new-posts');
    if (!b) {
      b = document.createElement('button');
      b.id = 'feed-new-posts';
      b.className = 'btn primary wide';
      b.addEventListener('click', applyPending);
      list.insertBefore(b, list.firstChild);
    }
    b.innerHTML = OM.icon('arrow-up', 16) + ' Новые публикации (' + F.pending.length + ') — показать';
  }

  function applyPending() {
    const list = byId('feed-list');
    const b = byId('feed-new-posts');
    if (b) b.remove();
    const posts = F.pending.filter(Boolean);
    const unknown = F.pending.some(p => !p);
    F.pending = [];
    if (unknown) { load(true); return; }
    if (!list) return;
    const em = list.querySelector('.empty-state');
    if (em) em.remove();
    /* вставка в обратном порядке: самая свежая публикация оказывается сверху */
    posts.forEach(p => list.insertAdjacentHTML('afterbegin', postHtml(p)));
  }

  let wsHooked = false;
  function hookRealtime() {
    if (wsHooked) return;
    wsHooked = true;
    OM.on('ws:post.new', d => noteNew(d && d.post ? d.post : null));
    OM.on('ws:story.new', () => noteNew(null));
  }
  hookRealtime();

  /* ------------------------------------------------------------------ инициализация раздела */

  function init() {
    if (F.inited) return;
    F.inited = true;

    const tabs = byId('feed-tabs');
    if (tabs) {
      tabs.addEventListener('click', e => {
        if (e.target.closest('.chip[data-reset]')) { resetSearch(); return; }
        const chip = e.target.closest('.chip[data-scope]');
        if (!chip) return;
        OM.state.feedScope = chip.dataset.scope || 'global';
        syncTabs();
        load(true);
      });
    }

    const search = byId('feed-search');
    if (search) search.addEventListener('keydown', e => {
      if (e.key === 'Enter') { e.preventDefault(); doSearch(); }
    });
    const sbtn = byId('feed-search-btn');
    if (sbtn) sbtn.addEventListener('click', doSearch);

    const nb = byId('post-new-btn');
    if (nb) nb.addEventListener('click', () => toggleComposer());

    const more = byId('feed-more');
    if (more) more.addEventListener('click', () => load(false));

    const list = byId('feed-list');
    if (list) {
      list.addEventListener('click', onListClick);
      list.addEventListener('keydown', onListKeydown);
    }

    const trends = byId('feed-trends');
    if (trends) trends.addEventListener('click', e => {
      const p = e.target.closest('.trend-pill[data-tag]');
      if (p) startTagSearch(p.dataset.tag);
    });

    hookRealtime();
  }

  function enter(params) {
    params = params || {};
    F.alive = true;
    if (params.scope) OM.state.feedScope = params.scope;
    if (params.q !== undefined) { OM.state.feedQuery = params.q || ''; OM.state.feedTag = ''; }
    if (params.tag !== undefined) { OM.state.feedTag = params.tag || ''; OM.state.feedQuery = ''; }

    init();
    syncTabs();

    const inp = byId('feed-search');
    if (inp) inp.value = searchActive() ? (OM.state.feedTag ? '#' + OM.state.feedTag : OM.state.feedQuery) : '';

    loadTrends();
    load(true);

    /* полоса историй живёт в stories.js */
    if (typeof OM.loadStories === 'function') {
      try { OM.loadStories(); } catch (e) {}
    }
  }

  function leave() {
    F.alive = false;
    F.seq++;
    F.loading = false;
    const box = byId('feed-composer');
    if (box) box.classList.add('hidden');
    resetDraft();
  }

  /* app.js подключается последним и заново создаёт OM.views,
     поэтому регистрируем раздел повторно после загрузки документа */
  function registerView() {
    OM.views = OM.views || {};
    OM.views.feed = { enter, leave };
  }
  registerView();
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', registerView);
  else setTimeout(registerView, 0);
})();
