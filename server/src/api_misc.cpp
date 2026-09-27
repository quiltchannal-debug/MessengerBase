// OrangeM - uploads, notifications, admin tools, health.
#include "api.h"
#include "crypto.h"
#include <fstream>
#include <sys/stat.h>

namespace om {

static int64_t g_started = nowSec();

static void upload(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  if (req.body.empty()) return res.fail(400, "пустой файл");
  std::string name = safeFileName(req.q("name", "file"));
  std::string kind = req.q("kind", "");
  if (kind.empty()) {
    std::string ct = toLower(req.header("content-type"));
    if (startsWith(ct, "image/")) kind = "image";
    else if (startsWith(ct, "video/")) kind = "video";
    else if (startsWith(ct, "audio/")) kind = "audio";
    else kind = "file";
  }
  if (req.body.size() > config().dataDir.size() * 0 + 32u * 1024 * 1024) return res.fail(413, "файл слишком большой");
  ::mkdir(config().uploadDir.c_str(), 0755);
  std::string stored = std::to_string(nowMs()) + "_" + randomHex(4) + "_" + name;
  std::string full = config().uploadDir + "/" + stored;
  if (!Server::writeFileAtomic(full, req.body)) return res.fail(500, "не удалось сохранить файл");
  Json r = Json::obj();
  r.set("ok", true);
  r.set("url", "/uploads/" + stored);
  r.set("kind", kind);
  r.set("size", (long long)req.body.size());
  r.set("name", name);
  res.json(r);
}

static void serveUpload(Request& req, Response& res) {
  std::string rel = req.params["file"];
  if (rel.find("..") != std::string::npos || rel.find('/') != std::string::npos) {
    res.fail(400, "bad path");
    return;
  }
  std::ifstream f(config().uploadDir + "/" + rel, std::ios::binary);
  if (!f) {
    res.fail(404, "файл не найден");
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  res.send(200, mimeFor(rel), ss.str());
  res.set("Cache-Control", "public, max-age=86400");
}

static void notificationsList(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  bool onlyUnread = req.q("unread", "0") == "1";
  std::string sql = "SELECT * FROM notifications WHERE user_id=?";
  if (onlyUnread) sql += " AND read=0";
  sql += " ORDER BY id DESC LIMIT 100";
  Json arr = Json::arr();
  for (auto& n : db().query(sql, {Json(req.user_id)})) {
    Json j = Json::obj();
    j.set("id", n.num("id"));
    j.set("kind", n.str("kind"));
    j.set("entity", n.str("entity"));
    j.set("entity_id", n.num("entity_id"));
    j.set("text", n.str("text"));
    j.set("read", n.num("read") != 0);
    j.set("created_at", n.num("created_at"));
    j.set("actor_id", n.num("actor_id"));
    Row a = getUserById(n.num("actor_id"));
    if (!a.f.empty())
      j.set("actor", Json::obj().set("id", a.num("id")).set("username", a.str("username"))
                           .set("display_name", a.str("display_name").empty() ? a.str("username")
                                                                              : a.str("display_name"))
                           .set("avatar", a.str("avatar")));
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("notifications", arr);
  r.set("unread", (long long)db().count("SELECT COUNT(*) FROM notifications WHERE user_id=? AND read=0",
                                        {Json(req.user_id)}));
  res.json(r);
}

static void notificationsRead(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  if (b.has("ids") && b["ids"].isArr()) {
    for (auto& id : b["ids"].a)
      db().exec("UPDATE notifications SET read=1 WHERE id=? AND user_id=?", {Json(id.num()), Json(req.user_id)});
  } else {
    db().exec("UPDATE notifications SET read=1 WHERE user_id=?", {Json(req.user_id)});
  }
  res.ok();
}

static void health(Request& req, Response& res) {
  resolveAuth(req);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("app", "OrangeM");
  r.set("version", "1.0.0");
  r.set("uptime", nowSec() - g_started);
  r.set("users", (long long)db().count("SELECT COUNT(*) FROM users"));
  r.set("online", (long long)hub().onlineCount());
  r.set("chats", (long long)db().count("SELECT COUNT(*) FROM chats"));
  r.set("messages", (long long)db().count("SELECT COUNT(*) FROM messages"));
  r.set("posts", (long long)db().count("SELECT COUNT(*) FROM posts WHERE deleted=0"));
  r.set("stories", (long long)db().count("SELECT COUNT(*) FROM stories WHERE deleted=0 AND expires_at>?",
                                         {Json(nowSec())}));
  r.set("communities", (long long)db().count("SELECT COUNT(*) FROM communities WHERE deleted=0"));
  r.set("server_time", nowSec());
  res.json(r);
}

static void adminGuard(Request& req, Response& res, bool& ok) {
  ok = false;
  if (!requireAuth(req, res)) return;
  Row u = getUserById(req.user_id);
  if (u.num("is_admin") == 0) {
    res.fail(403, "нужны права администратора");
    return;
  }
  ok = true;
}

static void adminStats(Request& req, Response& res) {
  bool ok = false;
  adminGuard(req, res, ok);
  if (!ok) return;
  Json r = Json::obj();
  r.set("ok", true);
  r.set("users", (long long)db().count("SELECT COUNT(*) FROM users"));
  r.set("users_today", (long long)db().count("SELECT COUNT(*) FROM users WHERE created_at>?",
                                             {Json(nowSec() - 86400)}));
  r.set("sessions_active", (long long)db().count("SELECT COUNT(*) FROM sessions WHERE revoked=0"));
  r.set("session_files", (long long)db().count("SELECT COUNT(*) FROM session_files WHERE revoked=0"));
  r.set("messages", (long long)db().count("SELECT COUNT(*) FROM messages"));
  r.set("posts", (long long)db().count("SELECT COUNT(*) FROM posts WHERE deleted=0"));
  r.set("communities", (long long)db().count("SELECT COUNT(*) FROM communities WHERE deleted=0"));
  r.set("online", (long long)hub().onlineCount());
  r.set("totp_enabled", (long long)db().count("SELECT COUNT(*) FROM users WHERE totp_enabled=1"));
  r.set("dev_codes", config().devCodes);
  res.json(r);
}

static void adminOutbox(Request& req, Response& res) {
  bool ok = false;
  adminGuard(req, res, ok);
  if (!ok) return;
  Json arr = Json::arr();
  for (auto& m : db().query("SELECT * FROM outbox ORDER BY id DESC LIMIT 100")) {
    Json j = Json::obj();
    j.set("id", m.num("id"));
    j.set("channel", m.str("channel"));
    j.set("target", m.str("target"));
    j.set("subject", m.str("subject"));
    j.set("body", m.str("body"));
    j.set("created_at", m.num("created_at"));
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("outbox", arr);
  res.json(r);
}

static void adminBan(Request& req, Response& res) {
  bool ok = false;
  adminGuard(req, res, ok);
  if (!ok) return;
  long long uid = atoll(req.params["id"].c_str());
  Json b = req.okBody();
  bool ban = b.has("ban") ? b["ban"].boolean(true) : true;
  db().exec("UPDATE users SET is_banned=? WHERE id=?", {Json(ban ? 1 : 0), Json(uid)});
  if (ban) db().exec("UPDATE sessions SET revoked=1 WHERE user_id=?", {Json(uid)});
  res.ok();
}

static void adminBroadcast(Request& req, Response& res) {
  bool ok = false;
  adminGuard(req, res, ok);
  if (!ok) return;
  Json b = req.okBody();
  std::string text = trim(b["text"].str());
  if (text.empty()) return res.fail(400, "пустое сообщение");
  Json ev = Json::obj();
  ev.set("type", "broadcast");
  ev.set("text", text);
  ev.set("at", nowSec());
  hub().broadcast(ev);
  res.ok();
}

static void adminUsers(Request& req, Response& res) {
  bool ok = false;
  adminGuard(req, res, ok);
  if (!ok) return;
  std::string q = trim(req.q("q"));
  std::string sql = "SELECT * FROM users";
  std::vector<Json> params;
  if (!q.empty()) {
    sql += " WHERE lower(username) LIKE ? OR lower(display_name) LIKE ? OR lower(ifnull(email,'')) LIKE ? OR phone LIKE ?";
    std::string like = "%" + toLower(q) + "%";
    params = {Json(like), Json(like), Json(like), Json(like)};
  }
  sql += " ORDER BY id DESC LIMIT 100";
  Json arr = Json::arr();
  for (auto& u : db().query(sql, params)) {
    Json j = userCard(u, req.user_id);
    j.set("email", u.str("email"));
    j.set("phone", u.str("phone"));
    j.set("banned", u.num("is_banned") != 0);
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("users", arr);
  res.json(r);
}

// realtime "ping" REST fallback (for clients without websockets)
static void eventsPoll(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json r = Json::obj();
  r.set("ok", true);
  r.set("online", (long long)hub().onlineCount());
  r.set("server_time", nowSec());
  r.set("unread_notifications", (long long)db().count(
      "SELECT COUNT(*) FROM notifications WHERE user_id=? AND read=0", {Json(req.user_id)}));
  res.json(r);
}

void registerMiscRoutes() {
  auto& r = router();
  r.add("POST", "/api/upload", upload);
  r.add("GET", "/uploads/:file", serveUpload);
  r.add("GET", "/api/notifications", notificationsList);
  r.add("POST", "/api/notifications/read", notificationsRead);
  r.add("POST", "/api/notifications", notificationsRead);
  r.add("GET", "/api/health", health);
  r.add("GET", "/api/events", eventsPoll);
  r.add("GET", "/api/admin/stats", adminStats);
  r.add("GET", "/api/admin/outbox", adminOutbox);
  r.add("GET", "/api/admin/users", adminUsers);
  r.add("POST", "/api/admin/users/:id/ban", adminBan);
  r.add("POST", "/api/admin/broadcast", adminBroadcast);
}

} // namespace om
