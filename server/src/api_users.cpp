// OrangeM - profiles, Orange ID, privacy, settings, follows, blocks, user search.
#include "api.h"
#include "crypto.h"

namespace om {

static Row resolveTarget(const std::string& key) {
  if (key.empty()) return Row{};
  bool numeric = true;
  for (char c : key)
    if (!isdigit((unsigned char)c)) { numeric = false; break; }
  if (numeric) {
    Row u = getUserById(atoll(key.c_str()));
    if (!u.f.empty()) return u;
  }
  return getUserByLogin(key);
}

static void me(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row u = getUserById(req.user_id);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("user", userSelf(u, req.user_id));
  r.set("online_friends", (long long)hub().onlineCount());
  res.json(r);
}

static void profile(Request& req, Response& res) {
  resolveAuth(req);
  Row u = resolveTarget(req.params["id"]);
  if (u.f.empty()) return res.fail(404, "пользователь не найден");
  if (!canViewProfile(req.user_id, u)) {
    Json r = Json::obj();
    r.set("ok", true);
    r.set("restricted", true);
    r.set("user", Json::obj().set("id", u.num("id")).set("username", u.str("username"))
                       .set("orange_id", u.str("orange_id")).set("avatar", u.str("avatar"))
                       .set("display_name", u.str("display_name")));
    return res.json(r);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("user", userCard(u, req.user_id));
  if (req.user_id == u.num("id")) r.set("user", userSelf(u, req.user_id));
  Row me = getUserById(req.user_id);
  r.set("stories", (long long)db().count(
      "SELECT COUNT(*) FROM stories WHERE user_id=? AND deleted=0 AND expires_at>?",
      {Json(u.num("id")), Json(nowSec())}));
  r.set("is_blocked_by_me", req.user_id ? isBlocked(req.user_id, u.num("id")) : false);
  r.set("blocked_me", req.user_id ? isBlocked(u.num("id"), req.user_id) : false);
  (void)me;
  res.json(r);
}

static void updateMe(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  Row u = getUserById(req.user_id);
  std::vector<std::string> sets;
  std::vector<Json> params;
  auto setField = [&](const std::string& col, const Json& val) {
    sets.push_back(col + "=?");
    params.push_back(val);
  };
  if (b.has("display_name")) {
    std::string dn = trim(b["display_name"].str());
    if (dn.size() > 64) return res.fail(400, "имя слишком длинное");
    setField("display_name", Json(dn));
  }
  if (b.has("bio")) {
    std::string bio = b["bio"].str();
    if (bio.size() > 500) return res.fail(400, "описание слишком длинное");
    setField("bio", Json(bio));
  }
  if (b.has("username")) {
    std::string un = trim(b["username"].str());
    if (!isUsername(un)) return res.fail(400, "логин: 3-32 символа (латиница, цифры, _ и .)");
    if (toLower(un) != toLower(u.str("username")) &&
        db().count("SELECT COUNT(*) FROM users WHERE lower(username)=lower(?)", {Json(un)}) > 0)
      return res.fail(409, "этот логин уже занят");
    setField("username", Json(un));
  }
  if (b.has("avatar")) setField("avatar", Json(b["avatar"].str()));
  if (b.has("banner")) setField("banner", Json(b["banner"].str()));
  if (b.has("presence")) {
    std::string p = b["presence"].str("online");
    if (p != "online" && p != "offline" && p != "invisible") return res.fail(400, "неизвестный режим");
    setField("presence", Json(p));
  }
  if (sets.empty()) return res.fail(400, "нет полей для обновления");
  params.push_back(Json(req.user_id));
  std::string sql = "UPDATE users SET " + [&] {
    std::string s;
    for (size_t i = 0; i < sets.size(); i++) { if (i) s += ","; s += sets[i]; }
    return s;
  }() + " WHERE id=?";
  if (!db().exec(sql, params)) return res.fail(500, "не удалось сохранить профиль");
  Row nu = getUserById(req.user_id);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("user", userSelf(nu, req.user_id));
  hub().toUser(req.user_id, Json::obj().set("type", "user.updated").set("user", userCard(nu, 0)));
  res.json(r);
}

static void updatePrivacy(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  Row u = getUserById(req.user_id);
  std::vector<std::string> sets;
  std::vector<Json> params;
  auto enumField = [&](const std::string& key, const std::string& col) {
    if (!b.has(key)) return true;
    std::string v = b[key].str();
    if (v != "everyone" && v != "contacts" && v != "nobody" && v != "followers") return false;
    sets.push_back(col + "=?");
    params.push_back(Json(v));
    return true;
  };
  if (!enumField("dm", "pr_dm")) return res.fail(400, "недопустимое значение dm");
  if (!enumField("stories", "pr_stories")) return res.fail(400, "недопустимое значение stories");
  if (!enumField("last_seen", "pr_last_seen")) return res.fail(400, "недопустимое значение last_seen");
  if (!enumField("online", "pr_online")) return res.fail(400, "недопустимое значение online");
  if (!enumField("profile", "pr_profile")) return res.fail(400, "недопустимое значение profile");
  if (b.has("read_receipts")) { sets.push_back("pr_read_receipts=?"); params.push_back(Json(b["read_receipts"].boolean() ? 1 : 0)); }
  if (b.has("find_phone")) { sets.push_back("pr_find_phone=?"); params.push_back(Json(b["find_phone"].boolean() ? 1 : 0)); }
  if (b.has("find_email")) { sets.push_back("pr_find_email=?"); params.push_back(Json(b["find_email"].boolean() ? 1 : 0)); }
  if (sets.empty()) return res.fail(400, "нет полей для обновления");
  std::string sql = "UPDATE users SET ";
  for (size_t i = 0; i < sets.size(); i++) { if (i) sql += ","; sql += sets[i]; }
  sql += " WHERE id=?";
  params.push_back(Json(req.user_id));
  db().exec(sql, params);
  Row nu = getUserById(req.user_id);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("user", userSelf(nu, req.user_id));
  res.json(r);
}

static void updateSettings(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  Row u = getUserById(req.user_id);
  Json cur = Json::obj();
  try { cur = Json::parse(u.str("settings", "{}")); } catch (...) {}
  Json patch = b.has("settings") ? b["settings"] : b;
  for (auto& kv : patch.o) cur.o[kv.first] = kv.second;
  if (cur.dumps().size() > 8000) return res.fail(400, "слишком большой объём настроек");
  db().exec("UPDATE users SET settings=? WHERE id=?", {Json(cur.dumps()), Json(req.user_id)});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("settings", cur);
  res.json(r);
}

static void followUser(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row t = resolveTarget(req.params["id"]);
  if (t.f.empty()) return res.fail(404, "пользователь не найден");
  long long tid = t.num("id");
  if (tid == req.user_id) return res.fail(400, "нельзя подписаться на себя");
  db().exec("INSERT OR IGNORE INTO follows(follower_id,followee_id,created_at) VALUES(?,?,?)",
            {Json(req.user_id), Json(tid), Json(nowSec())});
  addNotification(tid, "follow", req.user_id, "user", req.user_id, "подписался на вас");
  res.ok(Json::obj().set("following", true));
}

static void unfollowUser(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row t = resolveTarget(req.params["id"]);
  if (t.f.empty()) return res.fail(404, "пользователь не найден");
  db().exec("DELETE FROM follows WHERE follower_id=? AND followee_id=?",
            {Json(req.user_id), Json(t.num("id"))});
  res.ok(Json::obj().set("following", false));
}

static void followersList(Request& req, Response& res) {
  resolveAuth(req);
  Row t = resolveTarget(req.params["id"]);
  if (t.f.empty()) return res.fail(404, "пользователь не найден");
  bool followers = req.q("type", "followers") == "followers";
  std::string sql = followers
      ? "SELECT u.* FROM follows f JOIN users u ON u.id=f.follower_id WHERE f.followee_id=? ORDER BY f.created_at DESC LIMIT 200"
      : "SELECT u.* FROM follows f JOIN users u ON u.id=f.followee_id WHERE f.follower_id=? ORDER BY f.created_at DESC LIMIT 200";
  Json arr = Json::arr();
  for (auto& r : db().query(sql, {Json(t.num("id"))})) {
    if (isBlocked(req.user_id, r.num("id"))) continue;
    arr.push(userCard(r, req.user_id));
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("users", arr);
  res.json(r);
}

static void blockUser(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row t = resolveTarget(req.params["id"]);
  if (t.f.empty()) return res.fail(404, "пользователь не найден");
  db().exec("INSERT OR IGNORE INTO blocks(blocker_id,blocked_id,created_at) VALUES(?,?,?)",
            {Json(req.user_id), Json(t.num("id")), Json(nowSec())});
  db().exec("DELETE FROM follows WHERE (follower_id=? AND followee_id=?) OR (follower_id=? AND followee_id=?)",
            {Json(req.user_id), Json(t.num("id")), Json(t.num("id")), Json(req.user_id)});
  res.ok();
}

static void unblockUser(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row t = resolveTarget(req.params["id"]);
  if (t.f.empty()) return res.fail(404, "пользователь не найден");
  db().exec("DELETE FROM blocks WHERE blocker_id=? AND blocked_id=?",
            {Json(req.user_id), Json(t.num("id"))});
  res.ok();
}

static void blockedList(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json arr = Json::arr();
  for (auto& r : db().query("SELECT u.* FROM blocks b JOIN users u ON u.id=b.blocked_id "
                            "WHERE b.blocker_id=? ORDER BY b.created_at DESC", {Json(req.user_id)}))
    arr.push(userCard(r, req.user_id));
  Json r = Json::obj();
  r.set("ok", true);
  r.set("users", arr);
  res.json(r);
}

// ------------------------------------------------------------------ unified user search
Json searchUsers(const std::string& query, long long viewerId, int limit) {
  Json arr = Json::arr();
  std::string q = trim(query);
  if (q.empty()) return arr;
  std::string like = "%" + toLower(q) + "%";
  std::string phone = normalizePhone(q);
  std::string exactPhone = phone.empty() ? "\x01" : phone;
  std::string exactEmail = toLower(q);
  auto rows = db().query(
      "SELECT * FROM users WHERE is_banned=0 AND ("
      " lower(username) LIKE ? OR lower(display_name) LIKE ? OR lower(orange_id) LIKE ?"
      " OR lower(ifnull(email,'')) = ? OR phone = ? OR CAST(short_id AS TEXT) = ?"
      ") ORDER BY (CASE WHEN lower(username)=? THEN 0 WHEN upper(orange_id)=upper(?) THEN 1 ELSE 2 END), id LIMIT ?",
      {Json(like), Json(like), Json(like), Json(exactEmail), Json(exactPhone), Json(q), Json(toLower(q)),
       Json(q), Json((long long)limit)});
  for (auto& u : rows) {
    long long uid = u.num("id");
    if (viewerId && (isBlocked(viewerId, uid) || isBlocked(uid, viewerId))) continue;
    // privacy: phone/e-mail search only when allowed
    bool matchedByContact = toLower(u.str("email")) == exactEmail ||
                            (!phone.empty() && u.str("phone") == phone) ||
                            std::to_string(u.num("short_id")) == q;
    if (matchedByContact && viewerId != uid) {
      bool emailMatch = toLower(u.str("email")) == exactEmail;
      if (emailMatch && u.num("pr_find_email") == 0) continue;
      if (!emailMatch && u.num("pr_find_phone") == 0) continue;
    }
    if (!canViewProfile(viewerId, u)) {
      Json mini = Json::obj();
      mini.set("id", uid);
      mini.set("username", u.str("username"));
      mini.set("orange_id", u.str("orange_id"));
      mini.set("display_name", u.str("display_name"));
      mini.set("avatar", u.str("avatar"));
      mini.set("restricted", true);
      arr.push(mini);
      continue;
    }
    arr.push(userCard(u, viewerId));
  }
  return arr;
}

static void searchUsersRoute(Request& req, Response& res) {
  resolveAuth(req);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("users", searchUsers(req.q("q"), req.user_id, (int)req.qi("limit", 30)));
  res.json(r);
}

void registerUserRoutes() {
  auto& r = router();
  r.add("GET", "/api/me", me);
  r.add("GET", "/api/users/search", searchUsersRoute);
  r.add("GET", "/api/users/:id", profile);
  r.add("PATCH", "/api/users/me", updateMe);
  r.add("POST", "/api/users/me", updateMe);
  r.add("PATCH", "/api/users/me/privacy", updatePrivacy);
  r.add("POST", "/api/users/me/privacy", updatePrivacy);
  r.add("PATCH", "/api/users/me/settings", updateSettings);
  r.add("POST", "/api/users/me/settings", updateSettings);
  r.add("POST", "/api/users/:id/follow", followUser);
  r.add("DELETE", "/api/users/:id/follow", unfollowUser);
  r.add("GET", "/api/users/:id/follows", followersList);
  r.add("POST", "/api/users/:id/block", blockUser);
  r.add("DELETE", "/api/users/:id/block", unblockUser);
  r.add("GET", "/api/users/me/blocked", blockedList);
}

} // namespace om
