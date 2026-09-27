/* OrangeM — истории: полоса в ленте (#stories-bar), сетка (#view-stories),
   полноэкранный просмотрщик (#story-viewer) и создание истории */
window.OM = window.OM || {};

(function () {
  'use strict';

  const byId = id => document.getElementById(id);
  const STORY_MS = 5000;                       // автопереключение: 5 секунд
  const COLORS = ['#2f7ce0', '#ff7a1a', '#12b886', '#8b5cf6', '#e0397a', '#0f172a'];

  const S = {
    groups: [],        // [{user, items:[], has_unseen, count}]
    open: false,       // просмотрщик открыт
    gi: 0,             // индекс группы
    ii: 0,             // индекс истории в группе
    raf: null,         // requestAnimationFrame прогресса
    startedAt: 0,
    barBound: false,
    fullBound: false,
    viewerBound: false
  };

  /* ------------------------------------------------------------------ утилиты */

  /** Безопасный цвет фона (только hex / rgb / rgba) */
  function safeColor(c) {
    const v = String(c || '').trim();
    if (/^#[0-9a-fA-F]{3,8}$/.test(v)) return v;
    if (/^rgba?\(\s*[\d.,%\s]+\)$/.test(v)) return v;
    return '#2f7ce0';
  }

  function isVideo(story) {
    if (!story) return false;
    if (story.kind === 'video' || story.media_kind === 'video') return true;
    return /\.(mp4|webm|mov|m4v|ogv)(\?|#|$)/i.test(String(story.media || ''));
  }

  function groupUser(g) {
    if (!g) return {};
    return g.user || (g.items && g.items[0] && g.items[0].user) || {};
  }

  function displayName(u) {
    return (u && (u.display_name || u.username)) || 'Пользователь';
  }

  function isMine(g, st) {
    const me = OM.state.me;
    if (!me) return false;
    if (st && (st.is_mine === true || st.user_id === me.id)) return true;
    const u = groupUser(g);
    return !!u && u.id === me.id;
  }

  function shortText(t, n) {
    const s = String(t || '');
    return s.length > n ? s.slice(0, n - 1) + '…' : s;
  }

  function storyText(st) {
    return (st && (st.text || st.body || st.caption)) || '';
  }

  /* ------------------------------------------------------------------ данные */

  async function loadBar() {
    bindBar();
    bindFull();
    try {
      const r = await OM.api.get('/api/stories');
      S.groups = r.groups || [];
    } catch (e) {
      S.groups = [];
    }
    renderBar();
    renderFull();
    /* если просмотрщик открыт — не ломаем его, но сбрасываем неактуальные индексы */
    if (S.open && !S.groups[S.gi]) closeViewer();
  }

  /* ------------------------------------------------------------------ полоса историй в ленте */

  function addTile() {
    return `<div class="story-item" data-act="add" title="Добавить историю">
      <div class="story-add">+</div>
      <div class="cap">История</div>
    </div>`;
  }

  function renderBar() {
    const bar = byId('stories-bar');
    if (!bar) return;
    bar.innerHTML = addTile() + S.groups.map((g, i) => {
      const u = groupUser(g);
      return `<div class="story-item" data-gi="${i}" title="${OM.esc(displayName(u))}">
        ${OM.avatar(u, { size: 'lg', ring: true, seen: !g.has_unseen })}
        <div class="cap">${OM.esc(displayName(u))}</div>
      </div>`;
    }).join('');
  }

  function bindBar() {
    const bar = byId('stories-bar');
    if (!bar || S.barBound) return;
    S.barBound = true;
    bar.addEventListener('click', e => {
      if (e.target.closest('.story-item[data-act="add"]')) { openCreator(); return; }
      const item = e.target.closest('.story-item[data-gi]');
      if (item) openViewer(Number(item.dataset.gi));
    });
  }

  /* ------------------------------------------------------------------ сетка #view-stories */

  function renderFull() {
    const box = byId('stories-full');
    if (!box) return;
    if (!S.groups.length) {
      box.innerHTML = `<div class="empty-state">
        <img src="/assets/logo.jpg" class="empty-logo" alt="">
        <p>Пока нет историй. Поделитесь первым моментом!</p>
        <button class="btn primary" data-act="new">+ История</button>
      </div>`;
      return;
    }
    box.innerHTML = S.groups.map((g, i) => {
      const u = groupUser(g);
      const items = g.items || [];
      const first = items[0] || {};
      const thumb = first.kind === 'text' || !first.media
        ? `<div class="story-thumb" style="background:${safeColor(first.background)};display:flex;align-items:center;justify-content:center;padding:14px;text-align:center;font-weight:700;overflow:hidden">${OM.esc(shortText(storyText(first), 90))}</div>`
        : `<img class="story-thumb" src="${OM.esc(first.media)}" alt="" loading="lazy">`;
      return `<div class="story-card" data-gi="${i}">
        ${thumb}
        <div class="member-row">
          ${OM.avatar(u, { size: 'sm', ring: true, seen: !g.has_unseen })}
          <div style="flex:1;min-width:0">
            <div class="comm-name">${OM.esc(displayName(u))}</div>
            <div class="hint">${OM.fmtNum(items.length)} · ${OM.esc(OM.timeAgo(first.created_at))}</div>
          </div>
        </div>
        <button class="btn primary small" data-act="watch">Смотреть</button>
      </div>`;
    }).join('');
  }

  function bindFull() {
    const box = byId('stories-full');
    if (box && !S.fullBound) {
      S.fullBound = true;
      box.addEventListener('click', e => {
        if (e.target.closest('[data-act="new"]')) { openCreator(); return; }
        const card = e.target.closest('.story-card[data-gi]');
        if (card) openViewer(Number(card.dataset.gi));
      });
    }
    const nb = byId('story-new-btn');
    if (nb && !nb.dataset.bound) {
      nb.dataset.bound = '1';
      nb.addEventListener('click', openCreator);
    }
  }

  /* ------------------------------------------------------------------ просмотрщик */

  function activeFill() {
    const bar = byId('story-progress');
    if (!bar) return null;
    const item = bar.children[S.ii];
    return item ? item.querySelector('b') : null;
  }

  function stopProgress() {
    if (S.raf) { cancelAnimationFrame(S.raf); S.raf = null; }
  }

  function startProgress() {
    stopProgress();
    S.startedAt = performance.now();
    const tick = () => {
      if (!S.open) { S.raf = null; return; }
      const p = Math.min(1, (performance.now() - S.startedAt) / STORY_MS);
      const fill = activeFill();
      if (fill) fill.style.width = (p * 100).toFixed(2) + '%';
      if (p >= 1) { S.raf = null; nextStory(); return; }
      S.raf = requestAnimationFrame(tick);
    };
    S.raf = requestAnimationFrame(tick);
  }

  function renderStory() {
    const g = S.groups[S.gi];
    if (!g) { closeViewer(); return; }
    const items = g.items || [];
    if (!items.length) { closeViewer(); return; }
    if (S.ii >= items.length) { nextGroup(); return; }
    if (S.ii < 0) S.ii = 0;

    const st = items[S.ii] || {};
    const u = groupUser(g);
    const mine = isMine(g, st);

    const prog = byId('story-progress');
    if (prog) {
      prog.innerHTML = items.map((_, i) => `<i><b style="width:${i < S.ii ? 100 : 0}%"></b></i>`).join('');
    }

    let media;
    if (st.kind === 'text' || (!st.media && storyText(st))) {
      media = `<div class="txt" style="background:${safeColor(st.background)}">${OM.esc(storyText(st))}</div>`;
    } else if (!st.media) {
      media = `<div class="txt" style="background:${safeColor(st.background)}">История недоступна</div>`;
    } else if (isVideo(st)) {
      media = `<video src="${OM.esc(st.media)}" autoplay playsinline></video>`;
    } else {
      media = `<img src="${OM.esc(st.media)}" alt="">`;
    }

    const views = mine
      ? `<button class="btn ghost small" data-act="views" title="Кто смотрел">${OM.icon('eye', 16)} ${OM.fmtNum(st.views || 0)}</button>
         <button class="btn ghost small" data-act="del" title="Удалить историю">${OM.icon('trash', 16)}</button>`
      : '';

    const top = `<div class="story-top">
      <span data-act="profile" title="${OM.esc(displayName(u))}">${OM.avatar(u, { size: 'sm' })}</span>
      <div style="flex:1;min-width:0">
        <div class="comm-name">${OM.esc(displayName(u))}</div>
        <div class="hint">${OM.esc(OM.timeAgo(st.created_at))}</div>
      </div>
      ${views}
    </div>`;

    const caption = st.caption && st.kind !== 'text'
      ? `<div class="story-caption">${OM.esc(st.caption)}</div>`
      : '';

    const stage = byId('story-stage');
    if (stage) stage.innerHTML = top + media + caption;

    const prev = byId('story-prev');
    if (prev) prev.classList.toggle('hidden', S.gi === 0 && S.ii === 0);
    const next = byId('story-next');
    if (next) next.classList.toggle('hidden', S.gi === S.groups.length - 1 && S.ii === items.length - 1);

    startProgress();
    markViewed(st);
  }

  function nextStory() {
    const g = S.groups[S.gi];
    if (!g) { closeViewer(); return; }
    const items = g.items || [];
    if (S.ii < items.length - 1) { S.ii++; renderStory(); }
    else nextGroup();
  }

  function nextGroup() {
    if (S.gi < S.groups.length - 1) { S.gi++; S.ii = 0; renderStory(); }
    else closeViewer();
  }

  function prevStory() {
    if (S.ii > 0) { S.ii--; renderStory(); return; }
    if (S.gi > 0) {
      S.gi--;
      const g = S.groups[S.gi];
      S.ii = Math.max(0, (g.items || []).length - 1);
      renderStory();
      return;
    }
    renderStory(); // перезапуск первой истории
  }

  function openViewer(gi) {
    if (!S.groups.length) return;
    S.gi = Math.max(0, Math.min(Number(gi) || 0, S.groups.length - 1));
    S.ii = 0;
    S.open = true;
    bindViewer();
    const v = byId('story-viewer');
    if (v) v.classList.remove('hidden');
    renderStory();
  }

  function closeViewer() {
    S.open = false;
    stopProgress();
    const v = byId('story-viewer');
    if (v) v.classList.add('hidden');
    const stage = byId('story-stage');
    if (stage) stage.innerHTML = '';
    const prog = byId('story-progress');
    if (prog) prog.innerHTML = '';
    renderBar(); // обновляем кольца «просмотрено»
    renderFull();
  }

  function onStageClick(e) {
    const act = e.target.closest('[data-act]');
    if (act) {
      const a = act.dataset.act;
      if (a === 'profile') { openProfile(groupUser(S.groups[S.gi])); return; }
      if (a === 'views') { showViewers(); return; }
      if (a === 'del') { deleteStory(); return; }
      return;
    }
    const stage = byId('story-stage');
    if (!stage) return;
    const r = stage.getBoundingClientRect();
    if ((e.clientX - r.left) < r.width / 2) prevStory();
    else nextStory();
  }

  function bindViewer() {
    if (S.viewerBound) return;
    S.viewerBound = true;
    const close = byId('story-close');
    if (close) close.addEventListener('click', closeViewer);
    const prev = byId('story-prev');
    if (prev) prev.addEventListener('click', e => { e.stopPropagation(); prevStory(); });
    const next = byId('story-next');
    if (next) next.addEventListener('click', e => { e.stopPropagation(); nextStory(); });
    const stage = byId('story-stage');
    if (stage) stage.addEventListener('click', onStageClick);
  }

  /* Клавиатура: ← → Esc (слушаем один раз на документе) */
  document.addEventListener('keydown', e => {
    if (!S.open) return;
    if (e.key === 'Escape') { e.preventDefault(); closeViewer(); }
    else if (e.key === 'ArrowLeft') { e.preventDefault(); prevStory(); }
    else if (e.key === 'ArrowRight') { e.preventDefault(); nextStory(); }
  });

  function openProfile(u) {
    if (u && u.id) OM.navigate('profile', { id: u.id });
  }

  /* ------------------------------------------------------------------ действия над своей историей */

  async function markViewed(st) {
    if (!st || !st.id) return;
    const g = S.groups[S.gi];
    st.views = st.views || 0;
    try {
      const r = await OM.api.post('/api/stories/' + encodeURIComponent(st.id) + '/view');
      if (r && r.views != null) st.views = r.views;
      if (g) g.has_unseen = false;
    } catch (e) {
      /* отметка просмотра не критична — молча игнорируем */
    }
  }

  async function showViewers() {
    const g = S.groups[S.gi];
    const st = g && (g.items || [])[S.ii];
    if (!st) return;
    const m = OM.modal({ title: 'Кто смотрел', body: '<div class="hint">Загрузка…</div>' });
    try {
      const r = await OM.api.get('/api/stories/' + encodeURIComponent(st.id) + '/views');
      const viewers = r.viewers || [];
      m.body.innerHTML = viewers.length
        ? viewers.map(v => {
          const u = v.user || v;
          const when = v.created_at || v.viewed_at;
          return `<div class="member-row" data-uid="${OM.esc(u.id || '')}">
            ${OM.avatar(u, { size: 'sm' })}
            <div style="flex:1;min-width:0">
              <div class="comm-name">${OM.esc(displayName(u))}</div>
              <div class="hint">${OM.esc(when ? OM.timeAgo(when) : '')}</div>
            </div>
          </div>`;
        }).join('')
        : '<div class="hint">Пока никто не смотрел.</div>';
      m.body.querySelectorAll('.member-row[data-uid]').forEach(row => {
        row.style.cursor = 'pointer';
        row.addEventListener('click', () => {
          const uid = Number(row.dataset.uid);
          if (!uid) return;
          m.close();
          OM.navigate('profile', { id: uid });
        });
      });
    } catch (e) {
      m.body.innerHTML = `<div class="hint">${OM.esc(e.message || 'Не удалось загрузить список')}</div>`;
    }
  }

  function deleteStory() {
    const g = S.groups[S.gi];
    const st = g && (g.items || [])[S.ii];
    if (!st) return;
    OM.confirm('Удалить историю?', async () => {
      try {
        await OM.api.del('/api/stories/' + encodeURIComponent(st.id));
        OM.toast('История удалена', 'ok');
      } catch (e) {
        OM.toast(e.message || 'Не удалось удалить', 'err');
      }
      closeViewer();
      try { await loadBar(); } catch (e) {}
    }, 'Удалить');
  }

  /* ------------------------------------------------------------------ создание истории */

  function openCreator() {
    let kind = 'image';
    let media = '';
    let mediaKind = 'image';
    let bg = COLORS[0];

    const m = OM.modal({
      title: 'Новая история',
      body: `
        <div class="head-chips" id="st-kind">
          <button class="chip active" data-kind="image">Фото/видео</button>
          <button class="chip" data-kind="text">Текст</button>
        </div>
        <div class="field" id="st-media-field">
          <label>Файл</label>
          <input type="file" id="st-file" accept="image/*,video/*">
          <div class="hint">Поддерживаются изображения и видео. Файл загрузится на сервер OrangeM.</div>
        </div>
        <div id="st-preview"></div>
        <div class="field hidden" id="st-text-field">
          <label>Текст истории</label>
          <textarea id="st-text" maxlength="300" placeholder="Что происходит?"></textarea>
          <div class="hint">Цвет фона</div>
          <div class="head-chips" id="st-colors">
            ${COLORS.map(c => `<button class="chip${c === bg ? ' active' : ''}" data-color="${c}" style="background:${c};border-color:${c}" title="${c}">&nbsp;&nbsp;&nbsp;</button>`).join('')}
          </div>
        </div>
        <div class="field"><label>Подпись</label>
          <input class="input" id="st-caption" maxlength="200" placeholder="Необязательно"></div>
        <div class="field"><label>Кто видит</label>
          <select id="st-privacy">
            <option value="everyone">Все</option>
            <option value="contacts">Контакты</option>
            <option value="followers">Подписчики</option>
            <option value="nobody">Никто</option>
          </select></div>
        <div class="err hidden"></div>`,
      actions: [
        { label: 'Отмена', cls: 'ghost' },
        {
          label: 'Опубликовать', cls: 'primary', onClick: async mm => {
            const err = mm.body.querySelector('.err');
            const fail = t => { err.textContent = t; err.classList.remove('hidden'); };
            const captionEl = mm.body.querySelector('#st-caption');
            const textEl = mm.body.querySelector('#st-text');
            const caption = ((captionEl && captionEl.value) || '').trim();
            const text = ((textEl && textEl.value) || '').trim();
            const privacy = mm.body.querySelector('#st-privacy').value;
            const payload = { privacy };
            if (kind === 'text') {
              if (!text) { fail('Введите текст истории'); return; }
              payload.kind = 'text';
              payload.caption = text;
              payload.background = bg;
            } else {
              if (!media) { fail('Выберите фото или видео'); return; }
              payload.kind = 'image';
              payload.media = media;
              if (caption) payload.caption = caption;
            }
            try {
              await OM.api.post('/api/stories', payload);
              mm.close();
              OM.toast('История опубликована', 'ok');
              await loadBar();
            } catch (e) {
              fail(e.message || 'Не удалось опубликовать историю');
            }
          }
        }
      ]
    });

    /* переключатель «Фото/видео» ↔ «Текст» */
    const kindBox = m.body.querySelector('#st-kind');
    kindBox.addEventListener('click', e => {
      const chip = e.target.closest('.chip[data-kind]');
      if (!chip) return;
      kind = chip.dataset.kind;
      kindBox.querySelectorAll('.chip').forEach(c => c.classList.toggle('active', c === chip));
      m.body.querySelector('#st-media-field').classList.toggle('hidden', kind !== 'image');
      m.body.querySelector('#st-text-field').classList.toggle('hidden', kind !== 'text');
    });

    /* загрузка вложения */
    const file = m.body.querySelector('#st-file');
    file.addEventListener('change', async () => {
      const f = file.files && file.files[0];
      const prev = m.body.querySelector('#st-preview');
      if (!f) { media = ''; prev.innerHTML = ''; return; }
      mediaKind = /^video\//.test(f.type || '') ? 'video' : 'image';
      prev.innerHTML = '<div class="hint">Загрузка файла…</div>';
      try {
        const up = await OM.api.upload(f, mediaKind);
        media = up.url || '';
        prev.innerHTML = mediaKind === 'video'
          ? `<div class="post-media"><video src="${OM.esc(media)}" controls></video></div>`
          : `<div class="post-media"><img src="${OM.esc(media)}" alt=""></div>`;
      } catch (e) {
        media = '';
        prev.innerHTML = '';
        OM.toast(e.message || 'Не удалось загрузить файл', 'err');
      }
    });

    /* палитра фона для текстовой истории */
    const colors = m.body.querySelector('#st-colors');
    colors.addEventListener('click', e => {
      const chip = e.target.closest('.chip[data-color]');
      if (!chip) return;
      bg = chip.dataset.color;
      colors.querySelectorAll('.chip').forEach(c => c.classList.toggle('active', c === chip));
    });
  }

  /* ------------------------------------------------------------------ realtime */

  let wsHooked = false;
  function hookRealtime() {
    if (wsHooked) return;
    wsHooked = true;
    /* новая история — обновляем полосу и сетку; presence-события игнорируем */
    OM.on('ws:story.new', () => { loadBar(); });
  }

  /* ------------------------------------------------------------------ публичный интерфейс */

  OM.loadStories = loadBar;
  OM.openStories = function (groupId) {
    const run = () => {
      if (!S.groups.length) return;
      let gi = S.groups.findIndex(g => {
        const u = groupUser(g);
        return g.id === groupId || u.id === groupId || u.username === groupId;
      });
      if (gi < 0) gi = 0;
      openViewer(gi);
    };
    if (!S.groups.length) loadBar().then(run);
    else run();
  };

  OM.on('story-open', id => { OM.openStories(id); });

  const storiesView = {
    enter() {
      bindBar();
      bindFull();
      bindViewer();
      loadBar();
    },
    leave() {
      if (S.open) closeViewer();
      else stopProgress();
    }
  };

  /* app.js подключается последним и заново создаёт OM.views,
     поэтому регистрируем раздел ещё раз после загрузки документа */
  function registerView() {
    OM.views = OM.views || {};
    OM.views.stories = storiesView;
  }
  registerView();
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', registerView);
  else setTimeout(registerView, 0);

  hookRealtime();
})();
