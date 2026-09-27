// OrangeM - direct messages, group chats, realtime websocket layer.
#include "api.h"
#include "crypto.h"
#include <algorithm>

namespace om {

Json searchUsers(const std::string& q, long long viewerId, int limit);

// ------------------------------------------------------------------ helpers
static std::string dmKey(long long a, long long b) {
  if (a > b) std::swap(a, b);
  return "dm:" + std::to_string(a) + ":" + std::to_string(b);
}

void wsSendToUser(long long uid, const Json& msg) { hub().toUser(uid, msg); }

void wsSendToChat(long long chatId, const Json& msg, long long exceptUser) {
  for (long long uid : chatMemberIds(chatId))
    if (uid != exceptUser) hub().toUser(uid, msg);
}

void pushNewMessage(const Row& msgRow) {
  long long chatId = msgRow.num("chat_id");
  Json event = Json::obj();
  event.set("type", "message.new");
  event.set("chat_id", chatId);
  event.set("message", messageJson(msgRow, 0));
  for (long long uid : chatMemberIds(chatId)) {
    Json e = event;
    e.set("message", messageJson(msgRow, uid));
    e.set("unread", db().count("SELECT COUNT(*) FROM messages WHERE chat_id=? AND id>? AND sender_id<>? AND deleted=0",
                               {Json(chatId),
                                Json(db().queryOne("SELECT last_read FROM chat_members WHERE chat_id=? AND user_id=?",
                                                   {Json(chatId), Json(uid)}).num("last_read")),
                                Json(uid)}));
    hub().toUser(uid, e);
  }
}

void pushPresence(long long uid, bool online) {
  Row u = getUserById(uid);
  if (u.f.empty()) return;
  Json ev = Json::obj();
  ev.set("type", "presence");
  ev.set("user_id", uid);
  ev.set("online", online);
  ev.set("last_seen", nowSec());
  std::vector<long long> targets;
  for (auto& r : db().query("SELECT follower_id AS id FROM follows WHERE followee_id=? "
                            "UNION SELECT followee_id FROM follows WHERE follower_id=?",
                            {Json(uid), Json(uid)}))
    targets.push_back(r.num("id"));
  for (auto& r : db().query("SELECT DISTINCT m2.user_id AS id FROM chat_members m1 "
                            "JOIN chat_members m2 ON m1.chat_id=m2.chat_id WHERE m1.user_id=?",
                            {Json(uid)}))
    targets.push_back(r.num("id"));
  std::sort(targets.begin(), targets.end());
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  hub().toUsers(targets, ev);
}

static bool canWriteToChat(long long chatId, long long uid, std::string& err) {
  std::string role;
  if (!isChatMember(chatId, uid, &role)) { err = "вы не участник этого чата"; return false; }
  Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(chatId)});
  if (c.f.empty()) { err = "чат не найден"; return false; }
  if (c.str("kind") == "dm") {
    for (long long other : chatMemberIds(chatId)) {
      if (other == uid) continue;
      Row o = getUserById(other);
      if (o.f.empty()) continue;
      if (isBlocked(other, uid)) { err = "пользователь ограничил переписку"; return false; }
      if (isBlocked(uid, other)) { err = "вы заблокировали этого пользователя"; return false; }
      if (!o.str("pr_dm").empty() && o.str("pr_dm") == "nobody") {
        err = "пользователь закрыл личные сообщения";
        return false;
      }
    }
  }
  if (c.str("kind") == "channel" || (c.num("community_id") &&
      db().queryOne("SELECT kind FROM communities WHERE id=?", {Json(c.num("community_id"))}).str("kind") == "channel")) {
    if (role != "owner" && role != "admin") { err = "в канале пишет только администрация"; return false; }
  }
  return true;
}

static void chatSystemMessage(long long chatId, const std::string& text) {
  long long id = db().insert("INSERT INTO messages(chat_id,sender_id,body,system,created_at) VALUES(?,0,?,1,?)",
                             {Json(chatId), Json(text), Json(nowSec())});
  db().exec("UPDATE chats SET last_message_id=?,last_message_at=? WHERE id=?",
            {Json(id), Json(nowSec()), Json(chatId)});
  Row m = db().queryOne("SELECT * FROM messages WHERE id=?", {Json(id)});
  Json ev = Json::obj();
  ev.set("type", "message.new");
  ev.set("chat_id", chatId);
  ev.set("message", messageJson(m, 0));
  for (long long uid : chatMemberIds(chatId)) hub().toUser(uid, ev);
}

// ------------------------------------------------------------------ chats CRUD
static void chatsList(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  std::string q = toLower(trim(req.q("q")));
  Json arr = Json::arr();
  auto rows = db().query(
      "SELECT c.* FROM chats c JOIN chat_members m ON m.chat_id=c.id AND m.user_id=? AND m.left=0 "
      "ORDER BY c.last_message_at DESC LIMIT 200",
      {Json(req.user_id)});
  for (auto& c : rows) {
    if (c.str("kind") == "dm") {
      bool skip = false;
      for (long long other : chatMemberIds(c.num("id"))) {
        if (other == req.user_id) continue;
        if (isBlocked(other, req.user_id)) { skip = true; break; }
      }
      if (skip) continue;
    }
    Json cj = chatJson(c, req.user_id);
    if (!q.empty()) {
      std::string hay = toLower(cj["title"].str() + " " + cj["peer"]["username"].str() + " " +
                               cj["peer_card"]["orange_id"].str() + " " +
                               std::to_string(c.num("id")));
      if (hay.find(q) == std::string::npos) continue;
    }
    arr.push(cj);
  }
  std::sort(arr.a.begin(), arr.a.end(), [](const Json& a, const Json& b) {
    if (a["pinned"].boolean() != b["pinned"].boolean()) return a["pinned"].boolean();
    return a["last_message_at"].num() > b["last_message_at"].num();
  });
  Json r = Json::obj();
  r.set("ok", true);
  r.set("chats", arr);
  res.json(r);
}

static void chatCreate(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  std::string kind = b["kind"].str("dm");
  if (kind == "dm") {
    long long other = b["user_id"].num(0);
    if (!other && b.has("identifier")) {
      Row u = getUserByLogin(b["identifier"].str());
      if (u.f.empty()) return res.fail(404, "пользователь не найден");
      other = u.num("id");
    }
    if (!other) return res.fail(400, "укажите собеседника");
    if (other == req.user_id) return res.fail(400, "нельзя создать чат с самим собой");
    Row o = getUserById(other);
    if (o.f.empty()) return res.fail(404, "пользователь не найден");
    if (isBlocked(other, req.user_id)) return res.fail(403, "пользователь заблокировал вас");
    std::string pr = o.str("pr_dm", "everyone");
    if (pr == "nobody" || (pr == "contacts" && !isContact(req.user_id, other)))
      return res.fail(403, "пользователь ограничил личные сообщения");
    std::string key = dmKey(req.user_id, other);
    Row existing = db().queryOne("SELECT * FROM chats WHERE dm_key=?", {Json(key)});
    if (!existing.f.empty()) {
      for (long long m : chatMemberIds(existing.num("id"))) {
        if (m == req.user_id) {
          Json r = Json::obj();
          r.set("ok", true);
          r.set("chat", chatJson(existing, req.user_id));
          r.set("existed", true);
          return res.json(r);
        }
      }
      db().exec("INSERT OR REPLACE INTO chat_members(chat_id,user_id,role,joined_at,left) VALUES(?,?,'member',?,0)",
                {Json(existing.num("id")), Json(req.user_id), Json(nowSec())});
      Json r = Json::obj();
      r.set("ok", true);
      r.set("chat", chatJson(existing, req.user_id));
      return res.json(r);
    }
    long long id = db().insert("INSERT INTO chats(kind,created_by,dm_key,created_at,last_message_at) VALUES('dm',?,?,?,?)",
                               {Json(req.user_id), Json(key), Json(nowSec()), Json(nowSec())});
    db().exec("INSERT INTO chat_members(chat_id,user_id,role,joined_at) VALUES(?,?,'member',?)",
              {Json(id), Json(req.user_id), Json(nowSec())});
    db().exec("INSERT INTO chat_members(chat_id,user_id,role,joined_at) VALUES(?,?,'member',?)",
              {Json(id), Json(other), Json(nowSec())});
    Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
    Json cj = chatJson(c, req.user_id);
    hub().toUser(other, Json::obj().set("type", "chat.new").set("chat", chatJson(c, other)));
    Json r = Json::obj();
    r.set("ok", true);
    r.set("chat", cj);
    return res.json(r);
  }

  // group chat
  std::string title = trim(b["title"].str());
  if (title.empty() || title.size() > 100) return res.fail(400, "укажите название группы (до 100 символов)");
  long long id = db().insert("INSERT INTO chats(kind,title,avatar,description,created_by,created_at,last_message_at) "
                             "VALUES('group',?,?,?,?,?,?)",
                             {Json(title), Json(b["avatar"].str()), Json(b["description"].str()),
                              Json(req.user_id), Json(nowSec()), Json(nowSec())});
  db().exec("INSERT INTO chat_members(chat_id,user_id,role,joined_at) VALUES(?,?,'owner',?)",
            {Json(id), Json(req.user_id), Json(nowSec())});
  if (b.has("members") && b["members"].isArr()) {
    for (auto& m : b["members"].a) {
      long long uid = m.isNum() ? m.num() : 0;
      if (!uid && m.isStr()) {
        Row u = getUserByLogin(m.str());
        if (!u.f.empty()) uid = u.num("id");
      }
      if (!uid || uid == req.user_id) continue;
      db().exec("INSERT OR IGNORE INTO chat_members(chat_id,user_id,role,joined_at) VALUES(?,?,'member',?)",
                {Json(id), Json(uid), Json(nowSec())});
    }
  }
  Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
  chatSystemMessage(id, "Группа создана");
  for (long long uid : chatMemberIds(id))
    hub().toUser(uid, Json::obj().set("type", "chat.new").set("chat", chatJson(c, uid)));
  Json r = Json::obj();
  r.set("ok", true);
  r.set("chat", chatJson(c, req.user_id));
  res.json(r);
}

static void chatGet(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
  if (c.f.empty()) return res.fail(404, "чат не найден");
  if (!isChatMember(id, req.user_id)) return res.fail(403, "нет доступа");
  Json r = Json::obj();
  r.set("ok", true);
  r.set("chat", chatJson(c, req.user_id));
  Json members = Json::arr();
  for (long long uid : chatMemberIds(id)) {
    Row u = getUserById(uid);
    if (u.f.empty()) continue;
    Json mj = userCard(u, req.user_id);
    mj.set("role", db().queryOne("SELECT role FROM chat_members WHERE chat_id=? AND user_id=?", {Json(id), Json(uid)})
                        .str("role", "member"));
    members.push(mj);
  }
  r.set("members", members);
  res.json(r);
}

static void chatUpdate(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  std::string role;
  if (!isChatMember(id, req.user_id, &role)) return res.fail(403, "нет доступа");
  Json b = req.okBody();
  Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
  if (c.str("kind") == "dm") {
    if (b.has("pinned")) {
      db().exec("UPDATE chat_members SET pinned=? WHERE chat_id=? AND user_id=?",
                {Json(b["pinned"].boolean() ? 1 : 0), Json(id), Json(req.user_id)});
    }
    if (b.has("muted")) {
      db().exec("UPDATE chat_members SET muted=? WHERE chat_id=? AND user_id=?",
                {Json(b["muted"].boolean() ? 1 : 0), Json(id), Json(req.user_id)});
    }
    Json r = Json::obj();
    r.set("ok", true);
    r.set("chat", chatJson(db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)}), req.user_id));
    return res.json(r);
  }
  if (role != "owner" && role != "admin" && !req.is_admin) return res.fail(403, "нет прав");
  std::vector<std::string> sets;
  std::vector<Json> params;
  if (b.has("title")) { sets.push_back("title=?"); params.push_back(Json(b["title"].str())); }
  if (b.has("avatar")) { sets.push_back("avatar=?"); params.push_back(Json(b["avatar"].str())); }
  if (b.has("description")) { sets.push_back("description=?"); params.push_back(Json(b["description"].str())); }
  if (b.has("pinned")) {
    db().exec("UPDATE chat_members SET pinned=? WHERE chat_id=? AND user_id=?",
              {Json(b["pinned"].boolean() ? 1 : 0), Json(id), Json(req.user_id)});
  }
  if (b.has("muted")) {
    db().exec("UPDATE chat_members SET muted=? WHERE chat_id=? AND user_id=?",
              {Json(b["muted"].boolean() ? 1 : 0), Json(id), Json(req.user_id)});
  }
  if (!sets.empty()) {
    std::string sql = "UPDATE chats SET ";
    for (size_t i = 0; i < sets.size(); i++) { if (i) sql += ","; sql += sets[i]; }
    sql += " WHERE id=?";
    params.push_back(Json(id));
    db().exec(sql, params);
    Row nc = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
    for (long long uid : chatMemberIds(id))
      hub().toUser(uid, Json::obj().set("type", "chat.updated").set("chat", chatJson(nc, uid)));
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("chat", chatJson(db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)}), req.user_id));
  res.json(r);
}

static void chatMembersAdd(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  std::string role;
  if (!isChatMember(id, req.user_id, &role)) return res.fail(403, "нет доступа");
  Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
  if (c.str("kind") == "dm") return res.fail(400, "в личный чат нельзя добавить участника");
  if (role != "owner" && role != "admin" && !req.is_admin) return res.fail(403, "нет прав");
  Json b = req.okBody();
  std::vector<long long> add;
  if (b.has("user_id")) add.push_back(b["user_id"].num(0));
  if (b.has("identifier")) {
    Row u = getUserByLogin(b["identifier"].str());
    if (u.f.empty()) return res.fail(404, "пользователь не найден");
    add.push_back(u.num("id"));
  }
  if (b.has("members") && b["members"].isArr()) {
    for (auto& m : b["members"].a) {
      long long uid = m.isNum() ? m.num() : 0;
      if (!uid && m.isStr()) {
        Row u = getUserByLogin(m.str());
        if (!u.f.empty()) uid = u.num("id");
      }
      if (uid) add.push_back(uid);
    }
  }
  Json added = Json::arr();
  for (long long uid : add) {
    if (!uid) continue;
    Row u = getUserById(uid);
    if (u.f.empty()) continue;
    if (isChatMember(id, uid)) continue;
    db().exec("INSERT OR REPLACE INTO chat_members(chat_id,user_id,role,joined_at,left) VALUES(?,?,'member',?,0)",
              {Json(id), Json(uid), Json(nowSec())});
    chatSystemMessage(id, u.str("display_name").empty() ? u.str("username") + " присоединился"
                                                        : u.str("display_name") + " присоединился");
    hub().toUser(uid, Json::obj().set("type", "chat.new")
                          .set("chat", chatJson(db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)}), uid)));
    added.push(userCard(u, req.user_id));
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("added", added);
  res.json(r);
}

static void chatMembersRemove(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  long long target = atoll(req.params["uid"].c_str());
  std::string role;
  if (!isChatMember(id, req.user_id, &role)) return res.fail(403, "нет доступа");
  if (target != req.user_id && role != "owner" && role != "admin" && !req.is_admin)
    return res.fail(403, "нет прав");
  db().exec("DELETE FROM chat_members WHERE chat_id=? AND user_id=?", {Json(id), Json(target)});
  Row u = getUserById(target);
  chatSystemMessage(id, (u.str("display_name").empty() ? u.str("username") : u.str("display_name")) +
                            (target == req.user_id ? " покинул чат" : " удалён из чата"));
  hub().toUser(target, Json::obj().set("type", "chat.left").set("chat_id", id));
  res.ok();
}

static void chatLeave(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  if (!isChatMember(id, req.user_id)) return res.fail(403, "нет доступа");
  Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
  db().exec("DELETE FROM chat_members WHERE chat_id=? AND user_id=?", {Json(id), Json(req.user_id)});
  if (c.str("kind") == "dm") {
    db().exec("UPDATE chats SET last_message_at=? WHERE id=?", {Json(nowSec()), Json(id)});
  } else {
    Row u = getUserById(req.user_id);
    chatSystemMessage(id, (u.str("display_name").empty() ? u.str("username") : u.str("display_name")) +
                              " покинул чат");
  }
  hub().toUser(req.user_id, Json::obj().set("type", "chat.left").set("chat_id", id));
  res.ok();
}

// ------------------------------------------------------------------ messages
static void messagesList(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  if (!isChatMember(id, req.user_id)) return res.fail(403, "нет доступа");
  long long before = req.qi("cursor", 0);
  int limit = (int)req.qi("limit", 50);
  if (limit <= 0 || limit > 200) limit = 50;
  std::string q = trim(req.q("q"));
  std::string sql = "SELECT * FROM messages WHERE chat_id=?";
  std::vector<Json> params{Json(id)};
  if (!q.empty()) {
    sql += " AND lower(body) LIKE ?";
    params.push_back(Json("%" + toLower(q) + "%"));
  }
  if (before > 0) {
    sql += " AND id<?";
    params.push_back(Json(before));
  }
  sql += " ORDER BY id DESC LIMIT ?";
  params.push_back(Json((long long)limit));
  Json arr = Json::arr();
  long long oldest = 0;
  for (auto& m : db().query(sql, params)) {
    oldest = m.num("id");
    arr.push(messageJson(m, req.user_id));
  }
  std::reverse(arr.a.begin(), arr.a.end());
  Json r = Json::obj();
  r.set("ok", true);
  r.set("messages", arr);
  r.set("next_cursor", arr.size() == (size_t)limit ? Json(oldest) : Json());
  res.json(r);
}

static void messageSend(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  std::string err;
  if (!canWriteToChat(id, req.user_id, err)) return res.fail(403, err);
  Json b = req.okBody();
  std::string body = b["body"].str();
  std::string attachment = b["attachment"].str();
  std::string attachmentKind = b["attachment_kind"].str();
  if (trim(body).empty() && attachment.empty()) return res.fail(400, "сообщение пустое");
  if (body.size() > 8000) return res.fail(400, "сообщение слишком длинное");
  if (!rateLimit("msg:" + std::to_string(req.user_id), 240, 60))
    return res.fail(429, "слишком быстро, подождите секунду");
  long long replyTo = b["reply_to"].num(0);
  long long mid = db().insert(
      "INSERT INTO messages(chat_id,sender_id,body,attachment,attachment_kind,reply_to,created_at) "
      "VALUES(?,?,?,?,?,?,?)",
      {Json(id), Json(req.user_id), Json(body), Json(attachment), Json(attachmentKind), Json(replyTo),
       Json(nowSec())});
  db().exec("UPDATE chats SET last_message_id=?,last_message_at=? WHERE id=?",
            {Json(mid), Json(nowSec()), Json(id)});
  db().exec("UPDATE chat_members SET last_read=? WHERE chat_id=? AND user_id=?",
            {Json(mid), Json(id), Json(req.user_id)});
  Row m = db().queryOne("SELECT * FROM messages WHERE id=?", {Json(mid)});
  pushNewMessage(m);
  Row chat = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(id)});
  for (long long uid : chatMemberIds(id)) {
    if (uid == req.user_id) continue;
    Row mu = db().queryOne("SELECT muted FROM chat_members WHERE chat_id=? AND user_id=?", {Json(id), Json(uid)});
    if (mu.num("muted") != 0) continue;
    addNotification(uid, "message", req.user_id, "chat", id, body.substr(0, 120));
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("message", messageJson(m, req.user_id));
  res.json(r);
}

static void messageEdit(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long mid = atoll(req.params["id"].c_str());
  Row m = db().queryOne("SELECT * FROM messages WHERE id=?", {Json(mid)});
  if (m.f.empty()) return res.fail(404, "сообщение не найдено");
  if (m.num("sender_id") != req.user_id) return res.fail(403, "нет доступа");
  Json b = req.okBody();
  std::string body = b["body"].str();
  if (trim(body).empty()) return res.fail(400, "сообщение пустое");
  db().exec("UPDATE messages SET body=?,edited_at=? WHERE id=?", {Json(body), Json(nowSec()), Json(mid)});
  Row nm = db().queryOne("SELECT * FROM messages WHERE id=?", {Json(mid)});
  Json ev = Json::obj();
  ev.set("type", "message.edited");
  ev.set("chat_id", m.num("chat_id"));
  ev.set("message", messageJson(nm, 0));
  wsSendToChat(m.num("chat_id"), ev);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("message", messageJson(nm, req.user_id));
  res.json(r);
}

static void messageDelete(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long mid = atoll(req.params["id"].c_str());
  Row m = db().queryOne("SELECT * FROM messages WHERE id=?", {Json(mid)});
  if (m.f.empty()) return res.fail(404, "сообщение не найдено");
  std::string role;
  bool isMember = isChatMember(m.num("chat_id"), req.user_id, &role);
  if (!isMember) return res.fail(403, "нет доступа");
  if (m.num("sender_id") != req.user_id && role != "owner" && role != "admin" && !req.is_admin)
    return res.fail(403, "нет доступа");
  db().exec("UPDATE messages SET deleted=1,body='',attachment='' WHERE id=?", {Json(mid)});
  Json ev = Json::obj();
  ev.set("type", "message.deleted");
  ev.set("chat_id", m.num("chat_id"));
  ev.set("message_id", mid);
  wsSendToChat(m.num("chat_id"), ev);
  res.ok();
}

static void chatRead(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  if (!isChatMember(id, req.user_id)) return res.fail(403, "нет доступа");
  Json b = req.okBody();
  long long mid = b["message_id"].num(0);
  if (!mid) mid = db().queryOne("SELECT MAX(id) m FROM messages WHERE chat_id=?", {Json(id)}).num("m");
  db().exec("UPDATE chat_members SET last_read=? WHERE chat_id=? AND user_id=?",
            {Json(mid), Json(id), Json(req.user_id)});
  Row me = getUserById(req.user_id);
  Json ev = Json::obj();
  ev.set("type", "message.read");
  ev.set("chat_id", id);
  ev.set("message_id", mid);
  ev.set("user_id", req.user_id);
  ev.set("user", Json::obj().set("id", req.user_id).set("username", me.str("username"))
                    .set("display_name", me.str("display_name")).set("avatar", me.str("avatar")));
  wsSendToChat(id, ev, req.user_id);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("last_read", mid);
  res.json(r);
}

static void chatTyping(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  if (!isChatMember(id, req.user_id)) return res.fail(403, "нет доступа");
  Row me = getUserById(req.user_id);
  Json ev = Json::obj();
  ev.set("type", "typing");
  ev.set("chat_id", id);
  ev.set("user_id", req.user_id);
  ev.set("user",
         Json::obj().set("id", req.user_id).set("username", me.str("username"))
             .set("display_name", me.str("display_name").empty() ? me.str("username") : me.str("display_name"))
             .set("avatar", me.str("avatar")));
  wsSendToChat(id, ev, req.user_id);
  res.ok();
}

static void reactionToggle(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long mid = atoll(req.params["id"].c_str());
  Row m = db().queryOne("SELECT * FROM messages WHERE id=?", {Json(mid)});
  if (m.f.empty()) return res.fail(404, "сообщение не найдено");
  if (!isChatMember(m.num("chat_id"), req.user_id)) return res.fail(403, "нет доступа");
  Json b = req.okBody();
  std::string emoji = b["emoji"].str("\u2665");
  if (emoji.size() > 16) return res.fail(400, "некорректная реакция");
  Row ex = db().queryOne("SELECT 1 FROM reactions WHERE message_id=? AND user_id=? AND emoji=?",
                         {Json(mid), Json(req.user_id), Json(emoji)});
  bool added;
  if (ex.f.empty()) {
    db().exec("INSERT INTO reactions(message_id,user_id,emoji,created_at) VALUES(?,?,?,?)",
              {Json(mid), Json(req.user_id), Json(emoji), Json(nowSec())});
    added = true;
  } else {
    db().exec("DELETE FROM reactions WHERE message_id=? AND user_id=? AND emoji=?",
              {Json(mid), Json(req.user_id), Json(emoji)});
    added = false;
  }
  Row nm = db().queryOne("SELECT * FROM messages WHERE id=?", {Json(mid)});
  Json ev = Json::obj();
  ev.set("type", "message.reaction");
  ev.set("chat_id", m.num("chat_id"));
  ev.set("message_id", mid);
  ev.set("reactions", messageJson(nm, 0)["reactions"]);
  wsSendToChat(m.num("chat_id"), ev);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("added", added);
  r.set("reactions", messageJson(nm, req.user_id)["reactions"]);
  res.json(r);
}

// ------------------------------------------------------------------ chat search
static void chatSearch(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  std::string q = trim(req.q("q"));
  Json chats = Json::arr();
  Json users = Json::arr();
  if (q.empty()) {
    Json r = Json::obj();
    r.set("ok", true);
    r.set("chats", chats);
    r.set("users", users);
    return res.json(r);
  }
  // by numeric chat id
  try {
    long long cid = std::stoll(q);
    Row c = db().queryOne("SELECT * FROM chats WHERE id=?", {Json(cid)});
    if (!c.f.empty() && isChatMember(cid, req.user_id)) chats.push(chatJson(c, req.user_id));
  } catch (...) {}
  std::string like = "%" + toLower(q) + "%";
  for (auto& c : db().query(
           "SELECT c.* FROM chats c JOIN chat_members m ON m.chat_id=c.id AND m.user_id=? AND m.left=0 "
           "WHERE lower(c.title) LIKE ? ORDER BY c.last_message_at DESC LIMIT 30",
           {Json(req.user_id), Json(like)}))
    chats.push(chatJson(c, req.user_id));
  users = searchUsers(q, req.user_id, 30);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("chats", chats);
  r.set("users", users);
  res.json(r);
}

// ------------------------------------------------------------------ websocket
void registerWsRoutes() {
  router().addWs("/ws", [](Request& req, std::shared_ptr<WsConn> conn) {
    resolveAuth(req);
    if (!req.user_id) {
      conn->sendJson(Json::obj().set("type", "error").set("error", "unauthorized"));
      conn->closeConn();
      return;
    }
    long long uid = req.user_id;
    conn->user_id = uid;
    conn->token = req.token;
    hub().add(uid, conn);
    db().exec("UPDATE users SET last_seen=?,presence=CASE WHEN presence='invisible' THEN 'invisible' ELSE 'online' END "
              "WHERE id=?", {Json(nowSec()), Json(uid)});
    pushPresence(uid, true);
    {
      Row u = getUserById(uid);
      Json hello = Json::obj();
      hello.set("type", "ready");
      hello.set("user_id", uid);
      hello.set("orange_id", u.str("orange_id"));
      hello.set("server_time", nowSec());
      hello.set("online", (long long)hub().onlineCount());
      conn->sendJson(hello);
    }
    int ticks = 0;
    std::string pending;
    int pendingOp = 0;
    while (conn->open) {
      int opcode = 0;
      std::string payload;
      if (!wsReadFrame(conn->fd, opcode, payload, 20000)) break;
      if (opcode == -1) {
        ticks++;
        conn->sendRaw("", 0x9);  // ping
        if (ticks % 15 == 0) {   // ~5 минут: реальное время перешифровки ключевых файлов
          for (auto& f : db().query("SELECT uuid FROM session_files WHERE user_id=? AND revoked=0", {Json(uid)})) {
            std::string rotated = sessionFileRotate(uid, f.str("uuid"), "ws");
            if (rotated.empty()) continue;
            Json ev = Json::obj();
            ev.set("type", "session_file.update");
            ev.set("uuid", f.str("uuid"));
            ev.set("content", rotated);
            ev.set("at", nowSec());
            conn->sendJson(ev);
          }
          db().exec("UPDATE users SET last_seen=? WHERE id=?", {Json(nowSec()), Json(uid)});
        }
        continue;
      }
      if (opcode == 0x8) break;
      if (opcode == 0x9) { conn->sendRaw(payload, 0xA); continue; }
      if (opcode == 0xA) continue;
      if (opcode == 0x0) payload = pending + payload;
      if (opcode == 0x1 || opcode == 0x0) {
        Json msg;
        try { msg = Json::parse(payload); } catch (...) { continue; }
        std::string type = msg["type"].str();
        if (type == "ping") {
          conn->sendJson(Json::obj().set("type", "pong").set("t", nowSec()));
        } else if (type == "typing") {
          long long cid = msg["chat_id"].num(0);
          if (cid && isChatMember(cid, uid)) {
            Row me = getUserById(uid);
            Json ev = Json::obj();
            ev.set("type", "typing");
            ev.set("chat_id", cid);
            ev.set("user_id", uid);
            ev.set("user", Json::obj().set("id", uid).set("username", me.str("username"))
                              .set("display_name", me.str("display_name").empty() ? me.str("username")
                                                                                  : me.str("display_name"))
                              .set("avatar", me.str("avatar")));
            wsSendToChat(cid, ev, uid);
          }
        } else if (type == "read") {
          long long cid = msg["chat_id"].num(0);
          long long mid = msg["message_id"].num(0);
          if (cid && isChatMember(cid, uid)) {
            if (!mid) mid = db().queryOne("SELECT MAX(id) m FROM messages WHERE chat_id=?", {Json(cid)}).num("m");
            db().exec("UPDATE chat_members SET last_read=? WHERE chat_id=? AND user_id=?",
                      {Json(mid), Json(cid), Json(uid)});
            Json ev = Json::obj();
            ev.set("type", "message.read");
            ev.set("chat_id", cid);
            ev.set("message_id", mid);
            ev.set("user_id", uid);
            wsSendToChat(cid, ev, uid);
          }
        } else if (type == "session_file.sync") {
          std::string content = msg["content"].str();
          std::string uuid = msg["uuid"].str();
          if (!uuid.empty()) {
            std::string rotated = sessionFileRotate(uid, uuid, "ws-sync");
            if (!rotated.empty()) {
              Json ev = Json::obj();
              ev.set("type", "session_file.update");
              ev.set("uuid", uuid);
              ev.set("content", rotated);
              ev.set("at", nowSec());
              conn->sendJson(ev);
            }
          } else if (!content.empty()) {
            Json ev = Json::obj();
            ev.set("type", "session_file.ack");
            conn->sendJson(ev);
          }
        } else if (type == "presence") {
          std::string mode = msg["mode"].str("online");
          if (mode == "online" || mode == "offline" || mode == "invisible")
            db().exec("UPDATE users SET presence=? WHERE id=?", {Json(mode), Json(uid)});
        }
      }
      pending.clear();
      pendingOp = 0;
    }
    hub().remove(uid, conn);
    if (!hub().isOnline(uid)) {
      db().exec("UPDATE users SET last_seen=?,presence='offline' WHERE id=? AND presence<>'invisible'",
                {Json(nowSec()), Json(uid)});
      pushPresence(uid, false);
    }
  });
}

void registerChatRoutes() {
  auto& r = router();
  r.add("GET", "/api/chats", chatsList);
  r.add("POST", "/api/chats", chatCreate);
  r.add("GET", "/api/chats/search", chatSearch);
  r.add("GET", "/api/chats/:id", chatGet);
  r.add("PATCH", "/api/chats/:id", chatUpdate);
  r.add("POST", "/api/chats/:id", chatUpdate);
  r.add("POST", "/api/chats/:id/leave", chatLeave);
  r.add("POST", "/api/chats/:id/members", chatMembersAdd);
  r.add("DELETE", "/api/chats/:id/members/:uid", chatMembersRemove);
  r.add("GET", "/api/chats/:id/messages", messagesList);
  r.add("POST", "/api/chats/:id/messages", messageSend);
  r.add("POST", "/api/chats/:id/read", chatRead);
  r.add("POST", "/api/chats/:id/typing", chatTyping);
  r.add("PATCH", "/api/messages/:id", messageEdit);
  r.add("DELETE", "/api/messages/:id", messageDelete);
  r.add("POST", "/api/messages/:id/reactions", reactionToggle);
}

} // namespace om
