/* OrangeM — websocket-канал реального времени */
window.OM = window.OM || {};

OM.ws = (function () {
  let sock = null;
  let retry = 0;
  let closedByUser = false;
  let pingTimer = null;

  function url() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return proto + '//' + location.host + '/ws?token=' + encodeURIComponent(OM.api.getToken());
  }

  function connect() {
    if (!OM.api.hasToken()) return;
    if (sock && (sock.readyState === 0 || sock.readyState === 1)) return;
    closedByUser = false;
    try { sock = new WebSocket(url()); } catch (e) { scheduleRetry(); return; }

    sock.onopen = function () {
      retry = 0;
      OM.emit('ws:open', {});
      clearInterval(pingTimer);
      pingTimer = setInterval(() => { send({ type: 'ping' }); }, 25000);
    };
    sock.onmessage = function (ev) {
      let data;
      try { data = JSON.parse(ev.data); } catch (e) { return; }
      if (data && data.type) OM.emit('ws:' + data.type, data);
      OM.emit('ws', data);
    };
    sock.onclose = function () {
      clearInterval(pingTimer);
      OM.emit('ws:close', {});
      if (!closedByUser) scheduleRetry();
    };
    sock.onerror = function () { /* onclose обработает */ };
  }

  function scheduleRetry() {
    retry++;
    const delay = Math.min(15000, 800 * retry);
    setTimeout(connect, delay);
  }

  function send(obj) {
    if (sock && sock.readyState === 1) {
      try { sock.send(JSON.stringify(obj)); return true; } catch (e) { return false; }
    }
    return false;
  }

  function disconnect() {
    closedByUser = true;
    clearInterval(pingTimer);
    if (sock) { try { sock.close(); } catch (e) {} }
    sock = null;
  }

  return {
    connect, send, disconnect,
    isOpen: () => !!sock && sock.readyState === 1,
    typing(chatId) { send({ type: 'typing', chat_id: chatId }); },
    read(chatId, messageId) { send({ type: 'read', chat_id: chatId, message_id: messageId }); },
    syncSessionFile(uuid, content) { send({ type: 'session_file.sync', uuid, content }); }
  };
})();

/* Глобальные подписки на события сервера */
OM.on('ws:ready', d => {
  OM.emit('realtime-ready', d);
});
OM.on('ws:presence', d => {
  OM.state.onlineUsers[d.user_id] = !!d.online;
  if (OM.state.me && d.user_id === OM.state.me.id) return;
  OM.emit('presence-changed', d);
});
OM.on('ws:broadcast', d => {
  OM.toast('OrangeM: ' + d.text, 'ok', 6000);
});
