#include "api.h"
#include "util.h"
#include <fstream>

namespace om {

static Api g_api;
Api& api() { return g_api; }

static std::string boolStr(bool b) { return b ? "1" : "0"; }

Msg msgFromJson(const Json& m) {
  Msg r;
  r.id = m["id"].num();
  r.chatId = m["chat_id"].num();
  r.senderId = m["sender_id"].num(m["sender"]["id"].num());
  r.body = m["body"].str();
  r.attachment = m["attachment"].str();
  r.attachmentKind = m["attachment_kind"].str();
  r.createdAt = m["created_at"].num();
  r.mine = m["mine"].boolean();
  r.system = m["system"].boolean();
  r.deleted = m["deleted"].boolean();
  r.reads = m["reads"].num();
  r.senderName = m["sender"]["display_name"].str(m["sender"]["username"].str());
  return r;
}

std::string Api::sessionPath() { return appDataDir() + "\\session.dat"; }
std::string Api::settingsPath() { return appDataDir() + "\\settings.json"; }

bool Api::loadSettings() {
  std::string data;
  if (!readFileBytes(settingsPath(), data)) return false;
  try {
    Json j = Json::parse(data);
    std::string url = j["server"].str();
    if (!url.empty()) http.baseUrl = url;
    return true;
  } catch (...) {
    return false;
  }
}

void Api::saveSettings() {
  Json j = Json::obj();
  j.set("server", http.baseUrl);
  writeFileBytes(settingsPath(), j.dumps());
}

bool Api::loadSession() {
  std::string data;
  if (!readFileBytes(sessionPath(), data)) return false;
  data = trim(data);
  if (data.empty()) return false;
  http.token = data;
  return true;
}

void Api::saveSession(const std::string& token) { writeFileBytes(sessionPath(), token); }

void Api::clearSession() {
  http.token.clear();
  DeleteFileW(u2w(sessionPath()).c_str());
}

// ------------------------------------------------------------------ авторизация
bool Api::loginPassword(const std::string& identifier, const std::string& password, std::string& err) {
  Json body = Json::obj();
  body.set("identifier", identifier);
  body.set("password", password);
  HttpResponse r = http.post("/api/auth/login", body);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  Json j = r.json();
  if (j["need_totp"].boolean()) {
    needTotp = true;
    totpChallenge = j["challenge"].str();
    return true;
  }
  needTotp = false;
  std::string token = j["token"].str();
  if (token.empty()) {
    err = "Сервер не вернул токен";
    return false;
  }
  http.token = token;
  saveSession(token);
  me = userFromJson(j["user"]);
  return true;
}

bool Api::loginTotp(const std::string& code, std::string& err) {
  Json body = Json::obj();
  body.set("challenge", totpChallenge);
  body.set("code", code);
  HttpResponse r = http.post("/api/auth/login/totp", body);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  Json j = r.json();
  std::string token = j["token"].str();
  if (token.empty()) {
    err = "Сервер не вернул токен";
    return false;
  }
  http.token = token;
  saveSession(token);
  needTotp = false;
  me = userFromJson(j["user"]);
  return true;
}

bool Api::loginSessionFile(const std::string& fileContent, std::string& rotatedOut, std::string& err) {
  Json body = Json::obj();
  body.set("content", fileContent);
  body.set("device", "OrangeM Desktop (Windows)");
  HttpResponse r = http.post("/api/auth/session-file/login", body);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  Json j = r.json();
  std::string token = j["token"].str();
  if (token.empty()) {
    err = "Сервер не вернул токен";
    return false;
  }
  http.token = token;
  saveSession(token);
  me = userFromJson(j["user"]);
  rotatedOut = j["rotated_content"].str();
  return true;
}

bool Api::registerStart(const std::string& method, const std::string& contact, const std::string& username,
                        const std::string& password, std::string& targetOut, std::string& devCode,
                        std::string& err) {
  Json body = Json::obj();
  body.set("method", method);
  if (method == "phone") body.set("phone", contact);
  else body.set("email", contact);
  body.set("username", username);
  body.set("display_name", username);
  body.set("password", password);
  HttpResponse r = http.post("/api/auth/register", body);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  Json j = r.json();
  targetOut = j["target"].str();
  devCode = j["dev_code"].str();
  return true;
}

bool Api::registerVerify(const std::string& target, const std::string& code, std::string& err) {
  Json body = Json::obj();
  body.set("target", target);
  body.set("code", code);
  HttpResponse r = http.post("/api/auth/register/verify", body);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  Json j = r.json();
  std::string token = j["token"].str();
  if (token.empty()) {
    err = "Сервер не вернул токен";
    return false;
  }
  http.token = token;
  saveSession(token);
  me = userFromJson(j["user"]);
  return true;
}

bool Api::loadMe(std::string& err) {
  HttpResponse r = http.get("/api/me");
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  me = userFromJson(r.json()["user"]);
  return true;
}

void Api::logout() {
  http.post("/api/auth/logout", Json::obj());
  stopRealtime();
  clearSession();
  me = User{};
}

User Api::userFromJson(const Json& j) {
  User u;
  u.id = j["id"].num();
  u.orangeId = j["orange_id"].str();
  u.username = j["username"].str();
  u.displayName = j["display_name"].str(u.username);
  u.avatar = j["avatar"].str();
  u.bio = j["bio"].str();
  u.isAdmin = j["is_admin"].boolean();
  u.online = j["presence"]["online"].boolean();
  u.lastSeenText = j["presence"]["last_seen_text"].str();
  u.followers = j["followers"].num();
  u.following = j["following"].num();
  u.posts = j["posts"].num();
  return u;
}

// ------------------------------------------------------------------ чаты
std::vector<ChatItem> Api::chats(std::string& err, const std::string& query) {
  std::vector<ChatItem> out;
  std::string path = "/api/chats";
  if (!query.empty()) path += "?q=" + urlEncode(query);
  HttpResponse r = http.get(path);
  if (!r.ok()) {
    err = r.errorText();
    return out;
  }
  const Json doc = r.json();  // сначала сохраняем разобранный документ: ссылка на элемент
  // временного Json в range-for — источник падений
  for (auto& c : doc["chats"].a) {
    ChatItem ci;
    ci.id = c["id"].num();
    ci.kind = c["kind"].str("dm");
    ci.title = c["title"].str();
    ci.avatar = c["avatar"].str();
    ci.unread = c["unread"].num();
    ci.lastAt = c["last_message_at"].num();
    ci.pinned = c["pinned"].boolean();
    ci.muted = c["muted"].boolean();
    ci.peerId = c["peer"]["id"].num();
    ci.peerUsername = c["peer"]["username"].str();
    ci.peerOrangeId = c["peer_card"]["orange_id"].str();
    ci.peerOnline = c["peer_card"]["presence"]["online"].boolean();
    if (ci.title.empty()) ci.title = c["peer"]["display_name"].str(ci.peerUsername);
    const Json& lm = c["last_message"];
    if (!lm.isNull()) {
      std::string sender = lm["sender"]["display_name"].str(lm["sender"]["username"].str());
      std::string body = lm["body"].str();
      if (body.empty() && !lm["attachment"].str().empty()) body = "[вложение]";
      ci.preview = (c["kind"].str() == "dm" ? "" : sender + ": ") + body;
      if (!lm["created_at"].isNull()) ci.lastAt = lm["created_at"].num(ci.lastAt);
    }
    out.push_back(ci);
  }
  return out;
}

std::vector<Msg> Api::messages(long long chatId, std::string& err) {
  std::vector<Msg> out;
  HttpResponse r = http.get("/api/chats/" + std::to_string(chatId) + "/messages?limit=100");
  if (!r.ok()) {
    err = r.errorText();
    return out;
  }
  const Json doc = r.json();  // сначала сохраняем разобранный документ: ссылка на элемент
  // временного Json в range-for — источник падений
  for (auto& m : doc["messages"].a) out.push_back(msgFromJson(m));
  return out;
}

bool Api::sendMessage(long long chatId, const std::string& body, Msg& out, std::string& err) {
  Json b = Json::obj();
  b.set("body", body);
  HttpResponse r = http.post("/api/chats/" + std::to_string(chatId) + "/messages", b);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  out = msgFromJson(r.json()["message"]);
  return true;
}

bool Api::markRead(long long chatId, long long messageId) {
  Json b = Json::obj();
  b.set("message_id", messageId);
  return http.post("/api/chats/" + std::to_string(chatId) + "/read", b).ok();
}

bool Api::openDm(const std::string& identifier, long long& chatIdOut, std::string& err) {
  Json b = Json::obj();
  b.set("kind", "dm");
  b.set("identifier", identifier);
  HttpResponse r = http.post("/api/chats", b);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  chatIdOut = r.json()["chat"]["id"].num();
  return chatIdOut != 0;
}

// ------------------------------------------------------------------ лента
std::vector<Post> Api::feed(const std::string& scope, std::string& err, const std::string& query) {
  std::vector<Post> out;
  std::string path = "/api/feed?scope=" + scope + "&limit=30";
  if (!query.empty()) {
    if (query[0] == '#') path += "&tag=" + urlEncode(query.substr(1));
    else path += "&q=" + urlEncode(query);
  }
  HttpResponse r = http.get(path);
  if (!r.ok()) {
    err = r.errorText();
    return out;
  }
  const Json doc = r.json();  // сначала сохраняем разобранный документ: ссылка на элемент
  // временного Json в range-for — источник падений
  for (auto& p : doc["posts"].a) {
    Post po;
    po.id = p["id"].num();
    po.title = p["title"].str();
    po.body = p["body"].str();
    po.media = p["media"].str();
    po.likes = p["likes"].num();
    po.comments = p["comments"].num();
    po.views = p["views"].num();
    po.createdAt = p["created_at"].num();
    po.liked = p["liked"].boolean();
    po.isMine = p["is_mine"].boolean();
    po.authorId = p["author"]["id"].num();
    po.authorName = p["author"]["display_name"].str(p["author"]["username"].str());
    po.authorUsername = p["author"]["username"].str();
    po.authorAvatar = p["author"]["avatar"].str();
    if (!p["community"].isNull()) {
      po.isCommunity = true;
      po.communityName = p["community"]["name"].str();
    }
    for (auto& t : p["tags"].a) po.tags.push_back(t.str());
    out.push_back(po);
  }
  return out;
}

bool Api::createPost(const std::string& title, const std::string& body, std::string& err) {
  Json b = Json::obj();
  b.set("title", title);
  b.set("body", body);
  b.set("visibility", "public");
  HttpResponse r = http.post("/api/posts", b);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  return true;
}

bool Api::likePost(long long postId, bool like, std::string& err) {
  std::string path = "/api/posts/" + std::to_string(postId) + "/like";
  HttpResponse r = like ? http.post(path, Json::obj()) : http.del(path);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  return true;
}

bool Api::commentPost(long long postId, const std::string& body, std::string& err) {
  Json b = Json::obj();
  b.set("body", body);
  HttpResponse r = http.post("/api/posts/" + std::to_string(postId) + "/comments", b);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ сообщества
std::vector<Community> Api::communities(const std::string& scope, std::string& err, const std::string& query) {
  std::vector<Community> out;
  std::string path = "/api/communities?scope=" + scope;
  if (!query.empty()) path += "&q=" + urlEncode(query);
  HttpResponse r = http.get(path);
  if (!r.ok()) {
    err = r.errorText();
    return out;
  }
  const Json doc = r.json();  // сначала сохраняем разобранный документ: ссылка на элемент
  // временного Json в range-for — источник падений
  for (auto& c : doc["communities"].a) {
    Community cm;
    cm.id = c["id"].num();
    cm.name = c["name"].str();
    cm.slug = c["slug"].str();
    cm.description = c["description"].str();
    cm.avatar = c["avatar"].str();
    cm.kind = c["kind"].str("group");
    cm.orangeId = c["orange_id"].str();
    cm.members = c["members"].num();
    cm.isMember = c["is_member"].boolean();
    cm.isPublic = c["is_public"].boolean();
    cm.myRole = c["my_role"].str();
    cm.chatId = c["chat_id"].num();
    cm.ownerName = c["owner"]["display_name"].str(c["owner"]["username"].str());
    out.push_back(cm);
  }
  return out;
}

bool Api::joinCommunity(long long id, bool join, std::string& err) {
  std::string path = "/api/communities/" + std::to_string(id) + (join ? "/join" : "/leave");
  HttpResponse r = http.post(path, Json::obj());
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  return true;
}

bool Api::followUser(long long id, bool follow, std::string& err) {
  std::string path = "/api/users/" + std::to_string(id) + "/follow";
  HttpResponse r = follow ? http.post(path, Json::obj()) : http.del(path);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  return true;
}

std::vector<User> Api::searchUsers(const std::string& query, std::string& err) {
  std::vector<User> out;
  HttpResponse r = http.get("/api/users/search?q=" + urlEncode(query));
  if (!r.ok()) {
    err = r.errorText();
    return out;
  }
  const Json doc = r.json();  // сначала сохраняем разобранный документ: ссылка на элемент
  // временного Json в range-for — источник падений
  for (auto& u : doc["users"].a) out.push_back(userFromJson(u));
  return out;
}

// ------------------------------------------------------------------ истории
std::vector<StoryGroup> Api::stories(std::string& err) {
  std::vector<StoryGroup> out;
  HttpResponse r = http.get("/api/stories");
  if (!r.ok()) {
    err = r.errorText();
    return out;
  }
  const Json doc = r.json();  // сначала сохраняем разобранный документ: ссылка на элемент
  // временного Json в range-for — источник падений
  for (auto& g : doc["groups"].a) {
    StoryGroup sg;
    sg.userId = g["user"]["id"].num();
    sg.name = g["user"]["display_name"].str(g["user"]["username"].str());
    sg.avatar = g["user"]["avatar"].str();
    sg.hasUnseen = g["has_unseen"].boolean();
    sg.count = (int)g["count"].num();
    if (g["items"].size()) {
      const Json& s = g["items"].a[0];
      sg.id = s["id"].num();
      sg.caption = s["caption"].str();
      sg.media = s["media"].str();
      sg.background = s["background"].str();
      sg.kind = s["kind"].str("image");
      sg.views = s["views"].num();
      sg.createdAt = s["created_at"].num();
    }
    out.push_back(sg);
  }
  return out;
}

bool Api::viewStory(long long id) { return http.post("/api/stories/" + std::to_string(id) + "/view", Json::obj()).ok(); }

bool Api::createStory(const std::string& caption, const std::string& background, std::string& err) {
  Json b = Json::obj();
  b.set("kind", "text");
  b.set("caption", caption);
  b.set("background", background);
  b.set("privacy", "everyone");
  HttpResponse r = http.post("/api/stories", b);
  if (!r.ok()) {
    err = r.errorText();
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ realtime
bool Api::startRealtime(std::string& err) {
  if (http.token.empty()) {
    err = "Нет токена сессии";
    return false;
  }
  std::string ws = http.baseUrl;
  if (startsWith(ws, "https://")) ws = "wss://" + ws.substr(8);
  else if (startsWith(ws, "http://")) ws = "ws://" + ws.substr(7);
  size_t slash = ws.find('/', ws.find("//") + 2);
  if (slash != std::string::npos) ws = ws.substr(0, slash);
  ws += "/ws?token=" + urlEncode(http.token);
  ws_.setOnMessage([this](const std::string& s) {
    Json j;
    try { j = Json::parse(s); } catch (...) { return; }
    if (onEvent) onEvent(j);
  });
  return ws_.connect(ws, err);
}

void Api::stopRealtime() { ws_.disconnect(); }

void Api::realtimeTyping(long long chatId) {
  Json j = Json::obj();
  j.set("type", "typing");
  j.set("chat_id", chatId);
  ws_.send(j.dumps());
}

void Api::realtimeRead(long long chatId, long long messageId) {
  Json j = Json::obj();
  j.set("type", "read");
  j.set("chat_id", chatId);
  j.set("message_id", messageId);
  ws_.send(j.dumps());
}

} // namespace om
