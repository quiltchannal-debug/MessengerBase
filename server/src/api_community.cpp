// OrangeM - communities: groups & channels with roles, invites and search.
#include "api.h"
#include "crypto.h"

namespace om {

static Row communityByKey(const std::string& key) {
  if (key.empty()) return Row{};
  bool numeric = true;
  for (char c : key)
    if (!isdigit((unsigned char)c)) { numeric = false; break; }
  if (numeric) {
    Row c = db().queryOne("SELECT * FROM communities WHERE id=? AND deleted=0", {Json(atoll(key.c_str()))});
    if (!c.f.empty()) return c;
  }
  Row c = db().queryOne("SELECT * FROM communities WHERE lower(slug)=lower(?) AND deleted=0", {Json(key)});
  if (!c.f.empty()) return c;
  return db().queryOne("SELECT * FROM communities WHERE upper(orange_id)=upper(?) AND deleted=0", {Json(key)});
}

static std::string slugify(const std::string& name) {
  std::string out;
  for (char c : toLower(name)) {
    if (isalnum((unsigned char)c)) out += c;
    else if (c == ' ' || c == '-' || c == '_') out += '-';
  }
  while (!out.empty() && out.front() == '-') out.erase(out.begin());
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.size() > 40) out = out.substr(0, 40);
  if (out.size() < 3) out = "community-" + randomHex(3);
  return out;
}

static void communitiesList(Request& req, Response& res) {
  resolveAuth(req);
  std::string q = trim(req.q("q"));
  std::string scope = req.q("scope", "all");
  std::string sql = "SELECT * FROM communities WHERE deleted=0";
  std::vector<Json> params;
  if (scope == "mine") {
    if (!req.user_id) return res.fail(401, "требуется авторизация");
    sql += " AND id IN (SELECT community_id FROM community_members WHERE user_id=?)";
    params.push_back(Json(req.user_id));
  } else if (!req.user_id) {
    sql += " AND is_public=1";
  }
  if (!q.empty()) {
    sql += " AND (lower(name) LIKE ? OR lower(slug) LIKE ? OR lower(description) LIKE ? OR lower(orange_id) LIKE ? OR CAST(id AS TEXT)=?)";
    std::string like = "%" + toLower(q) + "%";
    params.push_back(Json(like));
    params.push_back(Json(like));
    params.push_back(Json(like));
    params.push_back(Json(like));
    params.push_back(Json(q));
  }
  sql += " ORDER BY members DESC, id DESC LIMIT 60";
  Json arr = Json::arr();
  for (auto& c : db().query(sql, params)) arr.push(communityJson(c, req.user_id));
  Json r = Json::obj();
  r.set("ok", true);
  r.set("communities", arr);
  res.json(r);
}

static void communityCreate(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  std::string name = trim(b["name"].str());
  if (name.size() < 2 || name.size() > 80) return res.fail(400, "название: от 2 до 80 символов");
  if (!rateLimit("comm:" + std::to_string(req.user_id), 10, 3600))
    return res.fail(429, "слишком много попыток создания");
  std::string slug = trim(b["slug"].str());
  slug = slug.empty() ? slugify(name) : slugify(slug);
  if (db().count("SELECT COUNT(*) FROM communities WHERE lower(slug)=lower(?)", {Json(slug)}) > 0)
    slug += "-" + randomHex(2);
  std::string kind = b["kind"].str("group");
  if (kind != "group" && kind != "channel") kind = "group";
  std::string orangeId;
  do { orangeId = "OMG-" + base32Encode(randomBytes(5)).substr(0, 7); }
  while (db().count("SELECT COUNT(*) FROM communities WHERE orange_id=?", {Json(orangeId)}) > 0);

  long long id = db().insert(
      "INSERT INTO communities(orange_id,slug,name,description,avatar,banner,kind,is_public,owner_id,members,created_at) "
      "VALUES(?,?,?,?,?,?,?,?,?,1,?)",
      {Json(orangeId), Json(slug), Json(name), Json(b["description"].str()), Json(b["avatar"].str()),
       Json(b["banner"].str()), Json(kind), Json(b["is_public"].boolean(true) ? 1 : 0), Json(req.user_id),
       Json(nowSec())});
  db().exec("INSERT INTO community_members(community_id,user_id,role,joined_at) VALUES(?,?,'owner',?)",
            {Json(id), Json(req.user_id), Json(nowSec())});
  long long chatId = db().insert(
      "INSERT INTO chats(kind,title,avatar,description,community_id,created_by,created_at,last_message_at) "
      "VALUES(?,?,?,?,?,?,?,?)",
      {Json(kind == "channel" ? "channel" : "group"), Json(name), Json(b["avatar"].str()),
       Json(b["description"].str()), Json(id), Json(req.user_id), Json(nowSec()), Json(nowSec())});
  db().exec("INSERT INTO chat_members(chat_id,user_id,role,joined_at) VALUES(?,?,'owner',?)",
            {Json(chatId), Json(req.user_id), Json(nowSec())});
  Row c = db().queryOne("SELECT * FROM communities WHERE id=?", {Json(id)});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("community", communityJson(c, req.user_id));
  r.set("chat_id", chatId);
  res.json(r);
}

static void communityGet(Request& req, Response& res) {
  resolveAuth(req);
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  if (c.num("is_public") == 0) {
    Row m = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                          {Json(c.num("id")), Json(req.user_id)});
    if (m.f.empty()) return res.fail(403, "сообщество закрытое");
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("community", communityJson(c, req.user_id));
  Json members = Json::arr();
  for (auto& m : db().query(
           "SELECT u.*, cm.role AS cm_role FROM community_members cm JOIN users u ON u.id=cm.user_id "
           "WHERE cm.community_id=? ORDER BY (CASE cm.role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END), cm.joined_at LIMIT 100",
           {Json(c.num("id"))})) {
    Json mj = userCard(m, req.user_id);
    mj.set("role", m.str("cm_role"));
    members.push(mj);
  }
  r.set("members", members);
  Json invites = Json::arr();
  Row myRole = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                             {Json(c.num("id")), Json(req.user_id)});
  if (!myRole.f.empty() && (myRole.str("role") == "owner" || myRole.str("role") == "admin")) {
    for (auto& i : db().query("SELECT * FROM community_invites WHERE community_id=?", {Json(c.num("id"))})) {
      Json ij = Json::obj();
      ij.set("code", i.str("code"));
      ij.set("uses", i.num("uses"));
      ij.set("max_uses", i.num("max_uses"));
      invites.push(ij);
    }
  }
  r.set("invites", invites);
  res.json(r);
}

static void communityUpdate(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  Row m = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                        {Json(c.num("id")), Json(req.user_id)});
  if (m.f.empty() || (m.str("role") != "owner" && m.str("role") != "admin"))
    if (!req.is_admin) return res.fail(403, "нет прав");
  Json b = req.okBody();
  std::vector<std::string> sets;
  std::vector<Json> params;
  if (b.has("name")) { sets.push_back("name=?"); params.push_back(Json(trim(b["name"].str()))); }
  if (b.has("description")) { sets.push_back("description=?"); params.push_back(Json(b["description"].str())); }
  if (b.has("avatar")) { sets.push_back("avatar=?"); params.push_back(Json(b["avatar"].str())); }
  if (b.has("banner")) { sets.push_back("banner=?"); params.push_back(Json(b["banner"].str())); }
  if (b.has("is_public")) { sets.push_back("is_public=?"); params.push_back(Json(b["is_public"].boolean() ? 1 : 0)); }
  if (b.has("kind")) {
    std::string k = b["kind"].str("group");
    if (k == "group" || k == "channel") { sets.push_back("kind=?"); params.push_back(Json(k)); }
  }
  if (!sets.empty()) {
    std::string sql = "UPDATE communities SET ";
    for (size_t i = 0; i < sets.size(); i++) { if (i) sql += ","; sql += sets[i]; }
    sql += " WHERE id=?";
    params.push_back(Json(c.num("id")));
    db().exec(sql, params);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("community", communityJson(db().queryOne("SELECT * FROM communities WHERE id=?", {Json(c.num("id"))}),
                                   req.user_id));
  res.json(r);
}

static void communityJoin(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  long long id = c.num("id");
  if (db().count("SELECT COUNT(*) FROM community_members WHERE community_id=? AND user_id=?",
                 {Json(id), Json(req.user_id)}) > 0) {
    Json r = Json::obj();
    r.set("ok", true);
    r.set("community", communityJson(c, req.user_id));
    return res.json(r);
  }
  if (c.num("is_public") == 0) return res.fail(403, "нужно приглашение");
  db().exec("INSERT INTO community_members(community_id,user_id,role,joined_at) VALUES(?,?,'member',?)",
            {Json(id), Json(req.user_id), Json(nowSec())});
  db().exec("UPDATE communities SET members=members+1 WHERE id=?", {Json(id)});
  Row chat = db().queryOne("SELECT * FROM chats WHERE community_id=? LIMIT 1", {Json(id)});
  if (!chat.f.empty()) {
    db().exec("INSERT OR REPLACE INTO chat_members(chat_id,user_id,role,joined_at,left) VALUES(?,?,'member',?,0)",
              {Json(chat.num("id")), Json(req.user_id), Json(nowSec())});
  }
  Row nc = db().queryOne("SELECT * FROM communities WHERE id=?", {Json(id)});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("community", communityJson(nc, req.user_id));
  res.json(r);
}

static void communityLeave(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  long long id = c.num("id");
  if (c.num("owner_id") == req.user_id) return res.fail(400, "владелец не может покинуть сообщество, передайте права");
  db().exec("DELETE FROM community_members WHERE community_id=? AND user_id=?", {Json(id), Json(req.user_id)});
  db().exec("UPDATE communities SET members=MAX(members-1,0) WHERE id=?", {Json(id)});
  Row chat = db().queryOne("SELECT * FROM chats WHERE community_id=? LIMIT 1", {Json(id)});
  if (!chat.f.empty())
    db().exec("DELETE FROM chat_members WHERE chat_id=? AND user_id=?", {Json(chat.num("id")), Json(req.user_id)});
  res.ok();
}

static void communityMemberRole(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  Row me = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                         {Json(c.num("id")), Json(req.user_id)});
  if (me.f.empty() || me.str("role") != "owner") return res.fail(403, "только владелец меняет роли");
  long long uid = atoll(req.params["uid"].c_str());
  Json b = req.okBody();
  std::string role = b["role"].str("member");
  if (role != "member" && role != "admin") return res.fail(400, "недопустимая роль");
  db().exec("UPDATE community_members SET role=? WHERE community_id=? AND user_id=?",
            {Json(role), Json(c.num("id")), Json(uid)});
  res.ok();
}

static void communityMemberRemove(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  Row me = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                         {Json(c.num("id")), Json(req.user_id)});
  bool privileged = !me.f.empty() && (me.str("role") == "owner" || me.str("role") == "admin");
  if (!privileged && !req.is_admin) return res.fail(403, "нет прав");
  long long uid = atoll(req.params["uid"].c_str());
  if (uid == c.num("owner_id")) return res.fail(400, "нельзя исключить владельца");
  db().exec("DELETE FROM community_members WHERE community_id=? AND user_id=?", {Json(c.num("id")), Json(uid)});
  db().exec("UPDATE communities SET members=MAX(members-1,0) WHERE id=?", {Json(c.num("id"))});
  Row chat = db().queryOne("SELECT * FROM chats WHERE community_id=? LIMIT 1", {Json(c.num("id"))});
  if (!chat.f.empty())
    db().exec("DELETE FROM chat_members WHERE chat_id=? AND user_id=?", {Json(chat.num("id")), Json(uid)});
  res.ok();
}

static void communityInvite(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  Row me = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                         {Json(c.num("id")), Json(req.user_id)});
  if (me.f.empty() || (me.str("role") != "owner" && me.str("role") != "admin"))
    if (!req.is_admin) return res.fail(403, "нет прав");
  Json b = req.okBody();
  std::string code = randomHex(5);
  db().exec("INSERT INTO community_invites(code,community_id,created_by,created_at,expires_at,max_uses) "
            "VALUES(?,?,?,?,?,?)",
            {Json(code), Json(c.num("id")), Json(req.user_id), Json(nowSec()),
             Json(b["expires_in"].num(0) ? nowSec() + b["expires_in"].num(0) : 0),
             Json(b["max_uses"].num(0))});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("code", code);
  r.set("link", config().publicUrl + "/join/" + code);
  res.json(r);
}

static void communityJoinByCode(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  std::string code = trim(req.params["code"]);
  Row i = db().queryOne("SELECT * FROM community_invites WHERE code=?", {Json(code)});
  if (i.f.empty()) return res.fail(404, "приглашение не найдено");
  if (i.num("expires_at") && i.num("expires_at") < nowSec()) return res.fail(400, "приглашение истекло");
  if (i.num("max_uses") && i.num("uses") >= i.num("max_uses")) return res.fail(400, "приглашение исчерпано");
  long long id = i.num("community_id");
  if (db().count("SELECT COUNT(*) FROM community_members WHERE community_id=? AND user_id=?",
                 {Json(id), Json(req.user_id)}) == 0) {
    db().exec("INSERT INTO community_members(community_id,user_id,role,joined_at) VALUES(?,?,'member',?)",
              {Json(id), Json(req.user_id), Json(nowSec())});
    db().exec("UPDATE communities SET members=members+1 WHERE id=?", {Json(id)});
    db().exec("UPDATE community_invites SET uses=uses+1 WHERE code=?", {Json(code)});
    Row chat = db().queryOne("SELECT * FROM chats WHERE community_id=? LIMIT 1", {Json(id)});
    if (!chat.f.empty())
      db().exec("INSERT OR REPLACE INTO chat_members(chat_id,user_id,role,joined_at,left) VALUES(?,?,'member',?,0)",
                {Json(chat.num("id")), Json(req.user_id), Json(nowSec())});
  }
  Row c = db().queryOne("SELECT * FROM communities WHERE id=?", {Json(id)});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("community", communityJson(c, req.user_id));
  res.json(r);
}

static void communityDelete(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row c = communityByKey(req.params["id"]);
  if (c.f.empty()) return res.fail(404, "сообщество не найдено");
  if (c.num("owner_id") != req.user_id && !req.is_admin) return res.fail(403, "нет прав");
  db().exec("UPDATE communities SET deleted=1 WHERE id=?", {Json(c.num("id"))});
  res.ok();
}

void registerCommunityRoutes() {
  auto& r = router();
  r.add("GET", "/api/communities", communitiesList);
  r.add("POST", "/api/communities", communityCreate);
  r.add("GET", "/api/communities/:id", communityGet);
  r.add("PATCH", "/api/communities/:id", communityUpdate);
  r.add("POST", "/api/communities/:id", communityUpdate);
  r.add("DELETE", "/api/communities/:id", communityDelete);
  r.add("POST", "/api/communities/:id/join", communityJoin);
  r.add("POST", "/api/communities/:id/leave", communityLeave);
  r.add("POST", "/api/communities/:id/members/:uid/role", communityMemberRole);
  r.add("DELETE", "/api/communities/:id/members/:uid", communityMemberRemove);
  r.add("POST", "/api/communities/:id/invites", communityInvite);
  r.add("POST", "/api/communities/join/:code", communityJoinByCode);
}

} // namespace om
