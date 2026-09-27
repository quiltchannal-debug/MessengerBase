#include "api.h"
#include "crypto.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace om {

static Config g_cfg;
Config& config() { return g_cfg; }

// ------------------------------------------------------------------ lookups
Row getUserById(long long id) {
  return db().queryOne("SELECT * FROM users WHERE id=?", {Json(id)});
}

Row getUserByLogin(const std::string& raw) {
  std::string ident = trim(raw);
  if (ident.empty()) return Row{};
  if (startsWith(toLower(ident), "om-")) {
    auto r = db().queryOne("SELECT * FROM users WHERE upper(orange_id)=upper(?)", {Json(ident)});
    if (!r.f.empty()) return r;
  }
  if (ident.find('@') != std::string::npos) {
    return db().queryOne("SELECT * FROM users WHERE lower(email)=lower(?)", {Json(ident)});
  }
  std::string phone = normalizePhone(ident);
  if (!phone.empty() && (ident[0] == '+' || isdigit((unsigned char)ident[0]))) {
    auto r = db().queryOne("SELECT * FROM users WHERE phone=?", {Json(phone)});
    if (!r.f.empty()) return r;
  }
  auto r = db().queryOne("SELECT * FROM users WHERE lower(username)=lower(?)", {Json(ident)});
  if (!r.f.empty()) return r;
  if (isdigit((unsigned char)ident[0])) {
    try {
      long long sid = std::stoll(ident);
      r = db().queryOne("SELECT * FROM users WHERE short_id=?", {Json(sid)});
      if (!r.f.empty()) return r;
      r = db().queryOne("SELECT * FROM users WHERE orange_id=?", {Json(ident)});
    } catch (...) {}
  }
  return r;
}

bool userExists(const std::string& username, const std::string& email, const std::string& phone) {
  if (!username.empty()) {
    if (db().count("SELECT COUNT(*) FROM users WHERE lower(username)=lower(?)", {Json(username)}) > 0) return true;
  }
  if (!email.empty()) {
    if (db().count("SELECT COUNT(*) FROM users WHERE lower(email)=lower(?)", {Json(email)}) > 0) return true;
  }
  if (!phone.empty()) {
    if (db().count("SELECT COUNT(*) FROM users WHERE phone=?", {Json(phone)}) > 0) return true;
  }
  return false;
}

// ------------------------------------------------------------------ relationships
bool isContact(long long a, long long b) {
  if (a == b) return true;
  long long mut = db().count(
      "SELECT COUNT(*) FROM follows f1 JOIN follows f2 ON f1.followee_id=f2.follower_id "
      "WHERE f1.follower_id=? AND f1.followee_id=? AND f2.followee_id=? AND f2.follower_id=?",
      {Json(a), Json(b), Json(a), Json(b)});
  if (mut > 0) return true;
  long long shared = db().count(
      "SELECT COUNT(*) FROM chat_members m1 JOIN chat_members m2 ON m1.chat_id=m2.chat_id "
      "JOIN chats c ON c.id=m1.chat_id WHERE m1.user_id=? AND m2.user_id=? AND c.kind='dm'",
      {Json(a), Json(b)});
  return shared > 0;
}

bool isBlocked(long long a, long long b) {
  return db().count("SELECT COUNT(*) FROM blocks WHERE blocker_id=? AND blocked_id=?",
                    {Json(a), Json(b)}) > 0;
}

bool canViewProfile(long long viewerId, const Row& target) {
  std::string pr = target.str("pr_profile", "everyone");
  if (pr == "everyone") return true;
  if (pr == "nobody") return viewerId == target.num("id");
  return isContact(viewerId, target.num("id"));
}

bool canSeeStory(long long viewerId, const Row& owner) {
  long long ownerId = owner.num("id");
  if (viewerId == ownerId) return true;
  if (isBlocked(ownerId, viewerId) || isBlocked(viewerId, ownerId)) return false;
  std::string pr = owner.str("pr_stories", "everyone");
  if (pr == "everyone") return true;
  if (pr == "nobody") return false;
  if (pr == "contacts") return isContact(viewerId, ownerId);
  if (pr == "followers")
    return db().count("SELECT COUNT(*) FROM follows WHERE follower_id=? AND followee_id=?",
                      {Json(viewerId), Json(ownerId)}) > 0;
  return true;
}

Json presenceFor(const Row& u, long long viewerId) {
  long long uid = u.num("id");
  bool online = hub().isOnline(uid);
  std::string prOnline = u.str("pr_online", "everyone");
  std::string prSeen = u.str("pr_last_seen", "everyone");
  bool self = viewerId == uid;
  bool contact = !self && isContact(viewerId, uid);
  Json j = Json::obj();
  bool showOnline = self || prOnline == "everyone" || (prOnline == "contacts" && contact);
  // invisible mode: user appears offline to everybody else
  if (!self && u.str("presence") == "invisible") showOnline = false;
  j.set("online", showOnline ? online : false);
  bool showSeen = self || prSeen == "everyone" || (prSeen == "contacts" && contact);
  long long lastSeen = u.num("last_seen");
  j.set("last_seen", showSeen ? Json(lastSeen) : Json());
  j.set("last_seen_text", showSeen && lastSeen > 0 ? Json(isoUtc(lastSeen)) : Json(""));
  j.set("hidden", !showSeen);
  return j;
}

std::vector<long long> chatMemberIds(long long chatId) {
  std::vector<long long> out;
  for (auto& r : db().query("SELECT user_id FROM chat_members WHERE chat_id=? AND left=0", {Json(chatId)}))
    out.push_back(r.num("user_id"));
  return out;
}

bool isChatMember(long long chatId, long long userId, std::string* role) {
  Row r = db().queryOne("SELECT role, left FROM chat_members WHERE chat_id=? AND user_id=?",
                        {Json(chatId), Json(userId)});
  if (r.f.empty() || r.num("left") == 1) return false;
  if (role) *role = r.str("role", "member");
  return true;
}

// ------------------------------------------------------------------ presentation
static Json miniUser(long long uid) {
  Json j = Json::obj();
  if (uid == 0) {
    j.set("id", 0);
    j.set("display_name", "Система");
    j.set("username", "system");
    j.set("avatar", "");
    j.set("orange_id", "");
    return j;
  }
  Row u = getUserById(uid);
  if (u.f.empty()) return j;
  j.set("id", uid);
  j.set("orange_id", u.str("orange_id"));
  j.set("username", u.str("username"));
  j.set("display_name", u.str("display_name").empty() ? u.str("username") : u.str("display_name"));
  j.set("avatar", u.str("avatar"));
  return j;
}

Json userCard(const Row& u, long long viewerId) {
  if (u.f.empty()) return Json();
  long long uid = u.num("id");
  Json j = Json::obj();
  j.set("id", uid);
  j.set("orange_id", u.str("orange_id"));
  j.set("short_id", u.num("short_id"));
  j.set("username", u.str("username"));
  j.set("display_name", u.str("display_name").empty() ? u.str("username") : u.str("display_name"));
  j.set("avatar", u.str("avatar"));
  j.set("banner", u.str("banner"));
  j.set("bio", u.str("bio"));
  j.set("created_at", u.num("created_at"));
  j.set("is_admin", u.num("is_admin") != 0);
  j.set("presence", presenceFor(u, viewerId));
  j.set("is_me", uid == viewerId);
  if (viewerId && viewerId != uid) {
    j.set("i_follow", db().count("SELECT COUNT(*) FROM follows WHERE follower_id=? AND followee_id=?",
                                 {Json(viewerId), Json(uid)}) > 0);
    j.set("follows_me", db().count("SELECT COUNT(*) FROM follows WHERE follower_id=? AND followee_id=?",
                                   {Json(uid), Json(viewerId)}) > 0);
    j.set("blocked", isBlocked(viewerId, uid));
    j.set("is_contact", isContact(viewerId, uid));
  }
  j.set("followers", db().count("SELECT COUNT(*) FROM follows WHERE followee_id=?", {Json(uid)}));
  j.set("following", db().count("SELECT COUNT(*) FROM follows WHERE follower_id=?", {Json(uid)}));
  j.set("posts", db().count("SELECT COUNT(*) FROM posts WHERE author_id=? AND deleted=0", {Json(uid)}));
  // privacy-gated contact info
  bool showEmail = viewerId == uid ||
                   (u.num("pr_find_email") != 0 &&
                    db().count("SELECT COUNT(*) FROM follows f1 JOIN follows f2 ON f1.followee_id=f2.follower_id "
                               "WHERE f1.follower_id=? AND f1.followee_id=? AND f2.followee_id=? AND f2.follower_id=?",
                               {Json(viewerId), Json(uid), Json(viewerId), Json(uid)}) > 0);
  bool showPhone = viewerId == uid ||
                   (u.num("pr_find_phone") != 0 &&
                    db().count("SELECT COUNT(*) FROM follows f1 JOIN follows f2 ON f1.followee_id=f2.follower_id "
                               "WHERE f1.follower_id=? AND f1.followee_id=? AND f2.followee_id=? AND f2.follower_id=?",
                               {Json(viewerId), Json(uid), Json(viewerId), Json(uid)}) > 0);
  if (showEmail && !u.str("email").empty()) j.set("email", u.str("email"));
  if (showPhone && !u.str("phone").empty()) j.set("phone", u.str("phone"));
  bool canDm = true;
  std::string pr = u.str("pr_dm", "everyone");
  if (viewerId != uid) {
    if (pr == "nobody") canDm = false;
    else if (pr == "contacts") canDm = isContact(viewerId, uid);
    if (isBlocked(uid, viewerId)) canDm = false;
  }
  j.set("can_dm", canDm);
  return j;
}

Json userSelf(const Row& u, long long viewerId) {
  Json j = userCard(u, viewerId);
  long long uid = u.num("id");
  j.set("email", u.str("email"));
  j.set("phone", u.str("phone"));
  j.set("email_verified", u.num("email_verified") != 0);
  j.set("phone_verified", u.num("phone_verified") != 0);
  j.set("totp_enabled", u.num("totp_enabled") != 0);
  j.set("presence_mode", u.str("presence", "online"));
  Json privacy = Json::obj();
  privacy.set("dm", u.str("pr_dm", "everyone"));
  privacy.set("stories", u.str("pr_stories", "everyone"));
  privacy.set("last_seen", u.str("pr_last_seen", "everyone"));
  privacy.set("online", u.str("pr_online", "everyone"));
  privacy.set("read_receipts", u.num("pr_read_receipts") != 0);
  privacy.set("find_phone", u.num("pr_find_phone") != 0);
  privacy.set("find_email", u.num("pr_find_email") != 0);
  privacy.set("profile", u.str("pr_profile", "everyone"));
  j.set("privacy", privacy);
  try {
    j.set("settings", Json::parse(u.str("settings", "{}")));
  } catch (...) {
    j.set("settings", Json::obj());
  }
  j.set("has_password", !u.str("password_hash").empty());
  j.set("session_files", (long long)db().count(
      "SELECT COUNT(*) FROM session_files WHERE user_id=? AND revoked=0", {Json(uid)}));
  return j;
}

Json postJson(const Row& p, long long viewerId) {
  Json j = Json::obj();
  long long id = p.num("id");
  long long authorId = p.num("author_id");
  j.set("id", id);
  j.set("author", miniUser(authorId));
  long long cid = p.num("community_id");
  j.set("community_id", cid ? Json(cid) : Json());
  if (cid) {
    Row c = db().queryOne("SELECT id,slug,name,avatar,orange_id FROM communities WHERE id=?", {Json(cid)});
    if (!c.f.empty()) {
      Json cj = Json::obj();
      cj.set("id", c.num("id"));
      cj.set("slug", c.str("slug"));
      cj.set("name", c.str("name"));
      cj.set("avatar", c.str("avatar"));
      cj.set("orange_id", c.str("orange_id"));
      j.set("community", cj);
    }
  }
  j.set("title", p.str("title"));
  j.set("body", p.str("body"));
  j.set("media", p.str("media"));
  j.set("media_kind", p.str("media_kind"));
  j.set("visibility", p.str("visibility", "public"));
  j.set("likes", p.num("likes"));
  j.set("comments", p.num("comments"));
  j.set("views", p.num("views"));
  j.set("created_at", p.num("created_at"));
  j.set("edited_at", p.num("edited_at"));
  Json tags = Json::arr();
  for (auto& t : split(trim(p.str("tags")), ' '))
    if (!t.empty()) tags.push(t);
  j.set("tags", tags);
  j.set("liked", viewerId ? db().count("SELECT COUNT(*) FROM post_likes WHERE post_id=? AND user_id=?",
                                       {Json(id), Json(viewerId)}) > 0 : false);
  j.set("is_mine", viewerId == authorId);
  return j;
}

Json messageJson(const Row& m, long long viewerId) {
  Json j = Json::obj();
  long long id = m.num("id");
  j.set("id", id);
  j.set("chat_id", m.num("chat_id"));
  j.set("sender", miniUser(m.num("sender_id")));
  j.set("sender_id", m.num("sender_id"));
  j.set("body", m.num("deleted") ? "" : m.str("body"));
  j.set("attachment", m.num("deleted") ? "" : m.str("attachment"));
  j.set("attachment_kind", m.str("attachment_kind"));
  j.set("reply_to", m.num("reply_to"));
  j.set("system", m.num("system") != 0);
  j.set("mine", m.num("sender_id") == viewerId);
  j.set("created_at", m.num("created_at"));
  j.set("edited_at", m.num("edited_at"));
  j.set("deleted", m.num("deleted") != 0);
  if (m.num("reply_to")) {
    Row r = db().queryOne("SELECT id,sender_id,body FROM messages WHERE id=?", {Json(m.num("reply_to"))});
    if (!r.f.empty()) {
      Json rj = Json::obj();
      rj.set("id", r.num("id"));
      rj.set("sender", miniUser(r.num("sender_id")));
      rj.set("body", r.str("body").substr(0, 200));
      j.set("reply", rj);
    }
  }
  Json reactions = Json::arr();
  for (auto& r : db().query("SELECT emoji, COUNT(*) c, SUM(CASE WHEN user_id=? THEN 1 ELSE 0 END) mine "
                            "FROM reactions WHERE message_id=? GROUP BY emoji",
                            {Json(viewerId), Json(id)})) {
    Json rj = Json::obj();
    rj.set("emoji", r.str("emoji"));
    rj.set("count", r.num("c"));
    rj.set("mine", r.num("mine") > 0);
    reactions.push(rj);
  }
  j.set("reactions", reactions);
  // read state for dm
  long long reads = db().count("SELECT COUNT(*) FROM message_reads WHERE message_id=?", {Json(id)});
  j.set("reads", reads);
  return j;
}

Json chatJson(const Row& c, long long viewerId) {
  Json j = Json::obj();
  long long cid = c.num("id");
  j.set("id", cid);
  j.set("kind", c.str("kind", "dm"));
  j.set("title", c.str("title"));
  j.set("avatar", c.str("avatar"));
  j.set("description", c.str("description"));
  j.set("created_at", c.num("created_at"));
  j.set("last_message_at", c.num("last_message_at"));
  j.set("community_id", c.num("community_id") ? Json(c.num("community_id")) : Json());
  Row me = db().queryOne("SELECT role,last_read,pinned,muted FROM chat_members WHERE chat_id=? AND user_id=?",
                         {Json(cid), Json(viewerId)});
  j.set("my_role", me.f.empty() ? "member" : me.str("role", "member"));
  j.set("pinned", !me.f.empty() && me.num("pinned") != 0);
  j.set("muted", !me.f.empty() && me.num("muted") != 0);
  long long lastRead = me.f.empty() ? 0 : me.num("last_read");
  j.set("last_read", lastRead);
  long long unread = db().count(
      "SELECT COUNT(*) FROM messages WHERE chat_id=? AND id>? AND sender_id<>? AND deleted=0",
      {Json(cid), Json(lastRead), Json(viewerId)});
  j.set("unread", unread);
  std::vector<long long> members = chatMemberIds(cid);
  j.set("members_count", (long long)members.size());
  if (c.str("kind") == "dm") {
    for (long long u : members) {
      if (u != viewerId) {
        Row pu = getUserById(u);
        if (!pu.f.empty()) {
          j.set("peer", miniUser(u));
          j.set("peer_card", userCard(pu, viewerId));
          if (c.str("title").empty()) j.set("title", pu.str("display_name").empty() ? pu.str("username")
                                                                                    : pu.str("display_name"));
          if (c.str("avatar").empty()) j.set("avatar", pu.str("avatar"));
        }
      }
    }
  }
  Row lm = db().queryOne("SELECT id,sender_id,body,attachment,attachment_kind,created_at,deleted,system "
                         "FROM messages WHERE chat_id=? ORDER BY id DESC LIMIT 1", {Json(cid)});
  if (!lm.f.empty()) {
    Json mj = Json::obj();
    mj.set("id", lm.num("id"));
    mj.set("sender_id", lm.num("sender_id"));
    mj.set("sender", miniUser(lm.num("sender_id")));
    mj.set("body", lm.num("deleted") ? "" : lm.str("body").substr(0, 160));
    mj.set("attachment", lm.str("attachment"));
    mj.set("attachment_kind", lm.str("attachment_kind"));
    mj.set("created_at", lm.num("created_at"));
    mj.set("system", lm.num("system") != 0);
    j.set("last_message", mj);
  }
  return j;
}

Json communityJson(const Row& c, long long viewerId) {
  Json j = Json::obj();
  long long id = c.num("id");
  j.set("id", id);
  j.set("orange_id", c.str("orange_id"));
  j.set("slug", c.str("slug"));
  j.set("name", c.str("name"));
  j.set("description", c.str("description"));
  j.set("avatar", c.str("avatar"));
  j.set("banner", c.str("banner"));
  j.set("kind", c.str("kind", "group"));
  j.set("is_public", c.num("is_public") != 0);
  j.set("members", c.num("members"));
  j.set("created_at", c.num("created_at"));
  j.set("owner", miniUser(c.num("owner_id")));
  Row m = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                        {Json(id), Json(viewerId)});
  j.set("is_member", !m.f.empty());
  j.set("my_role", m.f.empty() ? "" : m.str("role"));
  j.set("chat_id", db().count("SELECT id FROM chats WHERE community_id=? LIMIT 1", {Json(id)}) > 0
                       ? db().queryOne("SELECT id FROM chats WHERE community_id=? LIMIT 1", {Json(id)}).num("id")
                       : 0);
  j.set("posts", db().count("SELECT COUNT(*) FROM posts WHERE community_id=? AND deleted=0", {Json(id)}));
  return j;
}

Json storyJson(const Row& s, long long viewerId) {
  Json j = Json::obj();
  long long id = s.num("id");
  j.set("id", id);
  j.set("user", miniUser(s.num("user_id")));
  j.set("user_id", s.num("user_id"));
  j.set("media", s.str("media"));
  j.set("kind", s.str("kind", "image"));
  j.set("caption", s.str("caption"));
  j.set("background", s.str("background"));
  j.set("created_at", s.num("created_at"));
  j.set("expires_at", s.num("expires_at"));
  j.set("seen", viewerId ? db().count("SELECT COUNT(*) FROM story_views WHERE story_id=? AND viewer_id=?",
                                      {Json(id), Json(viewerId)}) > 0 : false);
  j.set("is_mine", s.num("user_id") == viewerId);
  j.set("views", db().count("SELECT COUNT(*) FROM story_views WHERE story_id=?", {Json(id)}));
  return j;
}

// ------------------------------------------------------------------ auth
std::string issueSession(long long uid, const std::string& kind, const std::string& ip,
                         const std::string& ua, const std::string& device) {
  std::string token = randomToken(32);
  db().exec("INSERT INTO sessions(token,user_id,kind,created_at,last_seen,ip,ua,device) VALUES(?,?,?,?,?,?,?,?)",
            {Json(token), Json(uid), Json(kind), Json(nowSec()), Json(nowSec()), Json(ip), Json(ua), Json(device)});
  return token;
}

bool resolveAuth(Request& req) {
  std::string token;
  std::string h = req.header("x-orange-token");
  if (!h.empty()) token = h;
  else {
    std::string a = req.header("authorization");
    if (startsWith(toLower(a), "bearer ")) token = trim(a.substr(7));
  }
  if (token.empty()) token = req.q("token");
  if (token.empty()) return false;
  Row s = db().queryOne("SELECT * FROM sessions WHERE token=?", {Json(token)});
  if (s.f.empty() || s.num("revoked") != 0) return false;
  req.user_id = s.num("user_id");
  req.token = token;
  req.auth_kind = s.str("kind");
  Row u = getUserById(req.user_id);
  if (u.f.empty() || u.num("is_banned") != 0) {
    req.user_id = 0;
    return false;
  }
  req.is_admin = u.num("is_admin") != 0;
  db().exec("UPDATE sessions SET last_seen=? WHERE token=?", {Json(nowSec()), Json(token)});
  return true;
}

bool requireAuth(Request& req, Response& res) {
  resolveAuth(req);
  if (req.user_id == 0) {
    res.fail(401, "требуется авторизация");
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ notifications
void addNotification(long long uid, const std::string& kind, long long actorId,
                     const std::string& entity, long long entityId, const std::string& text) {
  if (uid == actorId) return;
  db().exec("INSERT INTO notifications(user_id,kind,actor_id,entity,entity_id,text,created_at) VALUES(?,?,?,?,?,?,?)",
            {Json(uid), Json(kind), Json(actorId), Json(entity), Json(entityId), Json(text), Json(nowSec())});
  Json msg = Json::obj();
  msg.set("type", "notification");
  Json n = Json::obj();
  n.set("kind", kind);
  n.set("actor", actorId);
  n.set("entity", entity);
  n.set("entity_id", entityId);
  n.set("text", text);
  n.set("created_at", nowSec());
  msg.set("notification", n);
  hub().toUser(uid, msg);
}

// ------------------------------------------------------------------ rate limit
bool rateLimit(const std::string& key, int maxHits, int windowSec) {
  static std::mutex m;
  static std::map<std::string, std::vector<int64_t>> hits;
  std::lock_guard<std::mutex> lk(m);
  int64_t now = nowSec();
  auto& v = hits[key];
  v.erase(std::remove_if(v.begin(), v.end(), [&](int64_t t) { return now - t > windowSec; }), v.end());
  if ((int)v.size() >= maxHits) return false;
  v.push_back(now);
  if (hits.size() > 20000) hits.clear();
  return true;
}

// ------------------------------------------------------------------ delivery
static void smtpSend(const std::string& to, const std::string& subject, const std::string& body) {
  if (g_cfg.smtpHost.empty()) return;
  std::string cmd = "curl -s --max-time 15 --url 'smtp://" + g_cfg.smtpHost + "' " +
                    "--mail-from '" + g_cfg.smtpFrom + "' --mail-rcpt '" + to + "' " +
                    "--upload-file - --ssl-reqd";
  if (!g_cfg.smtpUser.empty())
    cmd += " --user '" + g_cfg.smtpUser + ":" + g_cfg.smtpPass + "'";
  cmd += " >/dev/null 2>&1";
  FILE* p = popen(cmd.c_str(), "w");
  if (!p) return;
  std::string msg = "From: OrangeM <" + g_cfg.smtpFrom + ">\r\nTo: " + to +
                    "\r\nSubject: " + subject + "\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n" + body + "\r\n";
  fwrite(msg.data(), 1, msg.size(), p);
  pclose(p);
}

void deliverCode(const std::string& channel, const std::string& target, const std::string& purpose,
                 const std::string& code, const std::string& extra) {
  std::string subject;
  std::string body;
  if (purpose == "register") {
    subject = "OrangeM: подтверждение регистрации";
    body = "Ваш код подтверждения OrangeM: " + code + "\nКод действует 10 минут.";
  } else if (purpose == "login") {
    subject = "OrangeM: код входа";
    body = "Ваш код входа OrangeM: " + code + "\nКод действует 10 минут.";
  } else if (purpose == "reset") {
    subject = "OrangeM: восстановление пароля";
    body = "Ваш код для смены пароля OrangeM: " + code + "\nКод действует 10 минут.";
  } else {
    subject = "OrangeM: код " + purpose;
    body = "Код: " + code;
  }
  db().exec("INSERT INTO outbox(channel,target,subject,body,created_at) VALUES(?,?,?,?,?)",
            {Json(channel), Json(target), Json(subject), Json(body), Json(nowSec())});
  if (channel == "email") smtpSend(target, subject, body);
}

} // namespace om
