// OrangeM - feed (posts), likes, comments, hashtag search and stories.
#include "api.h"
#include "crypto.h"

namespace om {

Json searchUsers(const std::string& q, long long viewerId, int limit);

static std::string visibilityFilter(long long viewerId, const std::string& scope) {
  if (scope == "following") return " AND (p.author_id=? OR p.author_id IN (SELECT followee_id FROM follows WHERE follower_id=?)) ";
  (void)viewerId;
  return "";
}

static void feed(Request& req, Response& res) {
  resolveAuth(req);
  long long before = req.qi("cursor", 0);
  int limit = (int)req.qi("limit", 20);
  if (limit <= 0 || limit > 50) limit = 20;
  std::string scope = req.q("scope", "global");
  std::string q = trim(req.q("q"));
  std::string tag = toLower(trim(req.q("tag")));
  if (!q.empty() && startsWith(q, "#")) { tag = toLower(q.substr(1)); q.clear(); }
  long long communityId = req.qi("community", 0);

  std::string sql = "SELECT p.* FROM posts p WHERE p.deleted=0";
  std::vector<Json> params;
  if (scope == "following") {
    if (!req.user_id) return res.fail(401, "требуется авторизация");
    sql += " AND (p.author_id=? OR p.author_id IN (SELECT followee_id FROM follows WHERE follower_id=?))";
    params.push_back(Json(req.user_id));
    params.push_back(Json(req.user_id));
  } else if (scope == "mine") {
    if (!req.user_id) return res.fail(401, "требуется авторизация");
    sql += " AND p.author_id=?";
    params.push_back(Json(req.user_id));
  } else if (scope == "user") {
    Row t = getUserById(req.qi("user", 0));
    if (t.f.empty()) t = getUserByLogin(req.q("user"));
    if (t.f.empty()) return res.fail(404, "пользователь не найден");
    sql += " AND p.author_id=?";
    params.push_back(Json(t.num("id")));
  } else if (scope == "community" || communityId) {
    long long cid = communityId ? communityId : req.qi("id", 0);
    sql += " AND p.community_id=?";
    params.push_back(Json(cid));
  }
  // Публичная видимость ограничивает только общую ленту и профиль
  if (scope == "global" || scope == "user") {
    sql += " AND (p.visibility='public' OR p.author_id=?)";
    params.push_back(Json(req.user_id));
  }
  if (!tag.empty()) {
    sql += " AND (p.tags LIKE ? OR lower(p.body) LIKE ? OR lower(p.title) LIKE ?)";
    params.push_back(Json("% " + tag + " %"));
    params.push_back(Json("%#" + tag + "%"));
    params.push_back(Json("%" + tag + "%"));
  } else if (!q.empty()) {
    sql += " AND (lower(p.title) LIKE ? OR lower(p.body) LIKE ? OR lower(p.tags) LIKE ?)";
    std::string like = "%" + toLower(q) + "%";
    params.push_back(Json(like));
    params.push_back(Json(like));
    params.push_back(Json(like));
  }
  if (before > 0) {
    sql += " AND p.id<?";
    params.push_back(Json(before));
  }
  sql += " ORDER BY p.id DESC LIMIT ?";
  params.push_back(Json((long long)limit));

  Json arr = Json::arr();
  long long lastId = 0;
  for (auto& p : db().query(sql, params)) {
    lastId = p.num("id");
    arr.push(postJson(p, req.user_id));
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("posts", arr);
  r.set("next_cursor", arr.size() == (size_t)limit ? Json(lastId) : Json());
  r.set("scope", scope);
  res.json(r);
}

static void trends(Request& req, Response& res) {
  resolveAuth(req);
  std::map<std::string, long long> counts;
  for (auto& p : db().query("SELECT tags FROM posts WHERE deleted=0 AND created_at>? LIMIT 3000",
                            {Json(nowSec() - 7 * 86400)})) {
    for (auto& t : split(trim(p.str("tags")), ' ')) {
      if (t.size() < 2) continue;
      counts[t]++;
    }
  }
  std::vector<std::pair<long long, std::string>> v;
  for (auto& kv : counts) v.push_back({kv.second, kv.first});
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
  Json arr = Json::arr();
  for (size_t i = 0; i < v.size() && i < 20; i++) {
    Json j = Json::obj();
    j.set("tag", v[i].second);
    j.set("posts", v[i].first);
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("trends", arr);
  res.json(r);
}

static void createPost(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  std::string title = trim(b["title"].str());
  std::string body = b["body"].str();
  if (title.empty() && body.empty() && b["media"].str().empty())
    return res.fail(400, "публикация не может быть пустой");
  if (title.size() > 200) return res.fail(400, "заголовок слишком длинный");
  if (body.size() > 20000) return res.fail(400, "текст слишком длинный");
  if (!rateLimit("post:" + std::to_string(req.user_id), 30, 3600))
    return res.fail(429, "слишком много публикаций, подождите");

  long long communityId = b["community_id"].num(0);
  std::string visibility = b["visibility"].str("public");
  if (visibility != "public" && visibility != "followers" && visibility != "community")
    visibility = "public";

  if (communityId) {
    Row m = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                          {Json(communityId), Json(req.user_id)});
    Row c = db().queryOne("SELECT * FROM communities WHERE id=? AND deleted=0", {Json(communityId)});
    if (c.f.empty()) return res.fail(404, "сообщество не найдено");
    if (m.f.empty() && c.str("kind") == "channel")
      return res.fail(403, "публиковать может только администрация");
    visibility = "community";
  }
  auto tags = extractTags(title + " " + body);
  std::string media = b["media"].str();
  std::string mediaKind = b["media_kind"].str();
  if (media.empty()) mediaKind.clear();

  long long id = db().insert(
      "INSERT INTO posts(author_id,community_id,title,body,tags,media,media_kind,visibility,created_at) "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      {Json(req.user_id), communityId ? Json(communityId) : Json(), Json(title), Json(body),
       Json(joinTags(tags)), Json(media), Json(mediaKind), Json(visibility), Json(nowSec())});
  if (!id) return res.fail(500, "не удалось создать публикацию");
  Row p = db().queryOne("SELECT * FROM posts WHERE id=?", {Json(id)});
  Json pj = postJson(p, req.user_id);

  if (communityId) {
    for (long long uid : ([&] {
           std::vector<long long> v;
           for (auto& m : db().query("SELECT user_id FROM community_members WHERE community_id=?", {Json(communityId)}))
             if (m.num("user_id") != req.user_id) v.push_back(m.num("user_id"));
           return v;
         }())) {
      addNotification(uid, "community_post", req.user_id, "post", id, "новая публикация в сообществе");
    }
  } else {
    for (auto& f : db().query("SELECT follower_id FROM follows WHERE followee_id=?", {Json(req.user_id)}))
      hub().toUser(f.num("follower_id"), Json::obj().set("type", "post.new").set("post", pj));
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("post", pj);
  res.json(r);
}

static void getPost(Request& req, Response& res) {
  resolveAuth(req);
  Row p = db().queryOne("SELECT * FROM posts WHERE id=? AND deleted=0", {Json(atoll(req.params["id"].c_str()))});
  if (p.f.empty()) return res.fail(404, "публикация не найдена");
  if (p.str("visibility") == "community" && p.num("community_id")) {
    Row m = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                          {Json(p.num("community_id")), Json(req.user_id)});
    Row c = db().queryOne("SELECT is_public FROM communities WHERE id=?", {Json(p.num("community_id"))});
    if (m.f.empty() && (c.f.empty() || c.num("is_public") == 0))
      return res.fail(403, "нет доступа к публикации");
  }
  db().exec("UPDATE posts SET views=views+1 WHERE id=?", {Json(p.num("id"))});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("post", postJson(p, req.user_id));
  res.json(r);
}

static void updatePost(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row p = db().queryOne("SELECT * FROM posts WHERE id=? AND deleted=0", {Json(id)});
  if (p.f.empty()) return res.fail(404, "публикация не найдена");
  if (p.num("author_id") != req.user_id && !req.is_admin) return res.fail(403, "нет доступа");
  Json b = req.okBody();
  std::string title = b.has("title") ? trim(b["title"].str()) : p.str("title");
  std::string body = b.has("body") ? b["body"].str() : p.str("body");
  std::string media = b.has("media") ? b["media"].str() : p.str("media");
  auto tags = extractTags(title + " " + body);
  db().exec("UPDATE posts SET title=?,body=?,tags=?,media=?,edited_at=? WHERE id=?",
            {Json(title), Json(body), Json(joinTags(tags)), Json(media), Json(nowSec()), Json(id)});
  Row np = db().queryOne("SELECT * FROM posts WHERE id=?", {Json(id)});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("post", postJson(np, req.user_id));
  res.json(r);
}

static void deletePost(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row p = db().queryOne("SELECT * FROM posts WHERE id=?", {Json(id)});
  if (p.f.empty()) return res.fail(404, "публикация не найдена");
  bool canDelete = p.num("author_id") == req.user_id || req.is_admin;
  if (!canDelete && p.num("community_id")) {
    Row m = db().queryOne("SELECT role FROM community_members WHERE community_id=? AND user_id=?",
                          {Json(p.num("community_id")), Json(req.user_id)});
    canDelete = !m.f.empty() && (m.str("role") == "owner" || m.str("role") == "admin");
  }
  if (!canDelete) return res.fail(403, "нет доступа");
  db().exec("UPDATE posts SET deleted=1 WHERE id=?", {Json(id)});
  res.ok();
}

static void likePost(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row p = db().queryOne("SELECT * FROM posts WHERE id=? AND deleted=0", {Json(id)});
  if (p.f.empty()) return res.fail(404, "публикация не найдена");
  db().exec("INSERT OR IGNORE INTO post_likes(post_id,user_id,created_at) VALUES(?,?,?)",
            {Json(id), Json(req.user_id), Json(nowSec())});
  long long likes = db().count("SELECT COUNT(*) FROM post_likes WHERE post_id=?", {Json(id)});
  db().exec("UPDATE posts SET likes=? WHERE id=?", {Json(likes), Json(id)});
  if (p.num("author_id") != req.user_id)
    addNotification(p.num("author_id"), "like", req.user_id, "post", id, "оценил вашу публикацию");
  res.ok(Json::obj().set("likes", likes).set("liked", true));
}

static void unlikePost(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  db().exec("DELETE FROM post_likes WHERE post_id=? AND user_id=?", {Json(id), Json(req.user_id)});
  long long likes = db().count("SELECT COUNT(*) FROM post_likes WHERE post_id=?", {Json(id)});
  db().exec("UPDATE posts SET likes=? WHERE id=?", {Json(likes), Json(id)});
  res.ok(Json::obj().set("likes", likes).set("liked", false));
}

static void commentsList(Request& req, Response& res) {
  resolveAuth(req);
  long long id = atoll(req.params["id"].c_str());
  Json arr = Json::arr();
  for (auto& c : db().query("SELECT * FROM post_comments WHERE post_id=? AND deleted=0 ORDER BY id ASC LIMIT 300",
                            {Json(id)})) {
    Json j = Json::obj();
    j.set("id", c.num("id"));
    j.set("post_id", c.num("post_id"));
    j.set("user", Json::obj()
                     .set("id", c.num("user_id"))
                     .set("username", getUserById(c.num("user_id")).str("username"))
                     .set("display_name", getUserById(c.num("user_id")).str("display_name"))
                     .set("avatar", getUserById(c.num("user_id")).str("avatar")));
    j.set("body", c.str("body"));
    j.set("created_at", c.num("created_at"));
    j.set("is_mine", c.num("user_id") == req.user_id);
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("comments", arr);
  res.json(r);
}

static void commentCreate(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Json b = req.okBody();
  std::string body = trim(b["body"].str());
  if (body.empty()) return res.fail(400, "комментарий пуст");
  if (body.size() > 2000) return res.fail(400, "комментарий слишком длинный");
  Row p = db().queryOne("SELECT * FROM posts WHERE id=? AND deleted=0", {Json(id)});
  if (p.f.empty()) return res.fail(404, "публикация не найдена");
  long long cid = db().insert("INSERT INTO post_comments(post_id,user_id,body,created_at) VALUES(?,?,?,?)",
                              {Json(id), Json(req.user_id), Json(body), Json(nowSec())});
  long long cnt = db().count("SELECT COUNT(*) FROM post_comments WHERE post_id=? AND deleted=0", {Json(id)});
  db().exec("UPDATE posts SET comments=? WHERE id=?", {Json(cnt), Json(id)});
  if (p.num("author_id") != req.user_id)
    addNotification(p.num("author_id"), "comment", req.user_id, "post", id, "прокомментировал вашу публикацию");
  Json r = Json::obj();
  r.set("ok", true);
  r.set("comment_id", cid);
  r.set("comments", cnt);
  res.json(r);
}

static void commentDelete(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row c = db().queryOne("SELECT * FROM post_comments WHERE id=?", {Json(id)});
  if (c.f.empty()) return res.fail(404, "комментарий не найден");
  if (c.num("user_id") != req.user_id && !req.is_admin) return res.fail(403, "нет доступа");
  db().exec("UPDATE post_comments SET deleted=1 WHERE id=?", {Json(id)});
  long long cnt = db().count("SELECT COUNT(*) FROM post_comments WHERE post_id=? AND deleted=0",
                             {Json(c.num("post_id"))});
  db().exec("UPDATE posts SET comments=? WHERE id=?", {Json(cnt), Json(c.num("post_id"))});
  res.ok();
}

// ------------------------------------------------------------------ stories
static void storiesFeed(Request& req, Response& res) {
  resolveAuth(req);
  int64_t now = nowSec();
  std::map<long long, std::vector<Row>> groups;
  std::vector<long long> order;
  std::string sql = "SELECT s.* FROM stories s WHERE s.deleted=0 AND s.expires_at>?";
  std::vector<Json> params{Json(now)};
  if (req.qi("user", 0)) {
    sql += " AND s.user_id=?";
    params.push_back(Json(req.qi("user", 0)));
  } else if (req.user_id) {
    sql += " AND (s.user_id=? OR s.user_id IN (SELECT followee_id FROM follows WHERE follower_id=?) "
           "OR s.user_id IN (SELECT user_id FROM chat_members WHERE chat_id IN "
           "(SELECT chat_id FROM chat_members WHERE user_id=?)))";
    params.push_back(Json(req.user_id));
    params.push_back(Json(req.user_id));
    params.push_back(Json(req.user_id));
  }
  sql += " ORDER BY s.id ASC LIMIT 500";
  for (auto& s : db().query(sql, params)) {
    Row owner = getUserById(s.num("user_id"));
    if (owner.f.empty()) continue;
    if (!canSeeStory(req.user_id, owner)) continue;
    if (isBlocked(req.user_id, owner.num("id"))) continue;
    if (groups.find(owner.num("id")) == groups.end()) order.push_back(owner.num("id"));
    groups[owner.num("id")].push_back(s);
  }
  Json arr = Json::arr();
  // own stories first, then unseen
  std::stable_sort(order.begin(), order.end(), [&](long long a, long long b) {
    if (a == req.user_id) return true;
    if (b == req.user_id) return false;
    auto seenOf = [&](long long uid) {
      for (auto& s : groups[uid])
        if (db().count("SELECT COUNT(*) FROM story_views WHERE story_id=? AND viewer_id=?",
                       {Json(s.num("id")), Json(req.user_id)}) == 0) return false;
      return true;
    };
    bool sa = seenOf(a), sb = seenOf(b);
    if (sa != sb) return !sa;
    return a > b;
  });
  for (long long uid : order) {
    Json g = Json::obj();
    Row owner = getUserById(uid);
    g.set("user", userCard(owner, req.user_id));
    Json items = Json::arr();
    bool unseen = false;
    for (auto& s : groups[uid]) {
      Json sj = storyJson(s, req.user_id);
      if (!sj["seen"].boolean()) unseen = true;
      items.push(sj);
    }
    g.set("items", items);
    g.set("has_unseen", unseen);
    g.set("count", (long long)groups[uid].size());
    arr.push(g);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("groups", arr);
  res.json(r);
}

static void storyCreate(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  std::string kind = b["kind"].str("image");
  std::string media = b["media"].str();
  std::string caption = b["caption"].str();
  std::string background = b["background"].str();
  if (kind == "image" && media.empty()) return res.fail(400, "выберите изображение или видео");
  if (kind == "text" && trim(caption).empty()) return res.fail(400, "введите текст истории");
  if (!rateLimit("story:" + std::to_string(req.user_id), 60, 3600))
    return res.fail(429, "слишком много историй");
  std::string privacy = b["privacy"].str("everyone");
  long long id = db().insert("INSERT INTO stories(user_id,media,kind,caption,background,privacy,created_at,expires_at) "
                             "VALUES(?,?,?,?,?,?,?,?)",
                             {Json(req.user_id), Json(media), Json(kind), Json(caption), Json(background),
                              Json(privacy), Json(nowSec()), Json(nowSec() + 86400)});
  Row s = db().queryOne("SELECT * FROM stories WHERE id=?", {Json(id)});
  Json sj = storyJson(s, req.user_id);
  for (auto& f : db().query("SELECT follower_id FROM follows WHERE followee_id=?", {Json(req.user_id)}))
    hub().toUser(f.num("follower_id"), Json::obj().set("type", "story.new").set("story", sj));
  Json r = Json::obj();
  r.set("ok", true);
  r.set("story", sj);
  res.json(r);
}

static void storyView(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row s = db().queryOne("SELECT * FROM stories WHERE id=? AND deleted=0", {Json(id)});
  if (s.f.empty()) return res.fail(404, "история не найдена");
  db().exec("INSERT OR IGNORE INTO story_views(story_id,viewer_id,viewed_at) VALUES(?,?,?)",
            {Json(id), Json(req.user_id), Json(nowSec())});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("views", (long long)db().count("SELECT COUNT(*) FROM story_views WHERE story_id=?", {Json(id)}));
  res.json(r);
}

static void storyViews(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row s = db().queryOne("SELECT * FROM stories WHERE id=?", {Json(id)});
  if (s.f.empty()) return res.fail(404, "история не найдена");
  if (s.num("user_id") != req.user_id) return res.fail(403, "нет доступа");
  Json arr = Json::arr();
  for (auto& v : db().query("SELECT * FROM story_views WHERE story_id=? ORDER BY viewed_at DESC LIMIT 500", {Json(id)})) {
    Row u = getUserById(v.num("viewer_id"));
    if (!u.f.empty()) arr.push(userCard(u, req.user_id));
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("viewers", arr);
  res.json(r);
}

static void storyDelete(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  Row s = db().queryOne("SELECT * FROM stories WHERE id=?", {Json(id)});
  if (s.f.empty()) return res.fail(404, "история не найдена");
  if (s.num("user_id") != req.user_id && !req.is_admin) return res.fail(403, "нет доступа");
  db().exec("UPDATE stories SET deleted=1 WHERE id=?", {Json(id)});
  res.ok();
}

// ------------------------------------------------------------------ combined search
static void unifiedSearch(Request& req, Response& res) {
  resolveAuth(req);
  std::string scope = req.q("scope", "all");
  std::string q = trim(req.q("q"));
  Json r = Json::obj();
  r.set("ok", true);
  r.set("scope", scope);
  r.set("q", q);
  if (q.empty()) {
    r.set("posts", Json::arr());
    r.set("users", Json::arr());
    r.set("chats", Json::arr());
    r.set("communities", Json::arr());
    return res.json(r);
  }
  if (scope == "all" || scope == "feed" || scope == "posts") {
    Json arr = Json::arr();
    std::string tag = "";
    std::string text = q;
    if (startsWith(q, "#")) { tag = toLower(q.substr(1)); text.clear(); }
    std::string sql = "SELECT * FROM posts WHERE deleted=0 AND (visibility='public'";
    std::vector<Json> params;
    if (req.user_id) { sql += " OR author_id=?"; params.push_back(Json(req.user_id)); }
    sql += ") AND (";
    if (!tag.empty()) {
      sql += "tags LIKE ? OR lower(body) LIKE ? OR lower(title) LIKE ?)";
      params.push_back(Json("% " + tag + " %"));
      params.push_back(Json("%#" + tag + "%"));
      params.push_back(Json("%" + tag + "%"));
    } else {
      sql += "lower(title) LIKE ? OR lower(body) LIKE ? OR lower(tags) LIKE ?)";
      std::string like = "%" + toLower(text) + "%";
      params.push_back(Json(like));
      params.push_back(Json(like));
      params.push_back(Json(like));
    }
    sql += " ORDER BY id DESC LIMIT 40";
    for (auto& p : db().query(sql, params)) arr.push(postJson(p, req.user_id));
    r.set("posts", arr);
  }
  if (scope == "all" || scope == "users" || scope == "chats") {
    r.set("users", searchUsers(q, req.user_id, 30));
  }
  res.json(r);
}

void registerFeedRoutes() {
  auto& r = router();
  r.add("GET", "/api/feed", feed);
  r.add("GET", "/api/feed/trends", trends);
  r.add("GET", "/api/search", unifiedSearch);
  r.add("POST", "/api/posts", createPost);
  r.add("GET", "/api/posts/:id", getPost);
  r.add("PATCH", "/api/posts/:id", updatePost);
  r.add("POST", "/api/posts/:id", updatePost);
  r.add("DELETE", "/api/posts/:id", deletePost);
  r.add("POST", "/api/posts/:id/like", likePost);
  r.add("DELETE", "/api/posts/:id/like", unlikePost);
  r.add("GET", "/api/posts/:id/comments", commentsList);
  r.add("POST", "/api/posts/:id/comments", commentCreate);
  r.add("DELETE", "/api/comments/:id", commentDelete);
  r.add("GET", "/api/stories", storiesFeed);
  r.add("POST", "/api/stories", storyCreate);
  r.add("GET", "/api/stories/:id/views", storyViews);
  r.add("POST", "/api/stories/:id/view", storyView);
  r.add("DELETE", "/api/stories/:id", storyDelete);
  (void)visibilityFilter;
}

} // namespace om
