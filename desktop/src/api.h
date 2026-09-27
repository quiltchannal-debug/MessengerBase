// OrangeM Desktop - модель данных и вызовы REST API.
#pragma once
#include <string>
#include <vector>
#include <functional>
#include "http.h"
#include "ws.h"
#include "json.h"

namespace om {

struct User {
  long long id = 0;
  std::string orangeId, username, displayName, avatar, bio;
  bool online = false, isAdmin = false;
  std::string lastSeenText;
  long long followers = 0, following = 0, posts = 0;
};

struct ChatItem {
  long long id = 0;
  std::string kind = "dm", title, avatar, preview;
  int64_t lastAt = 0;
  long long unread = 0;
  bool pinned = false, muted = false;
  long long peerId = 0;
  bool peerOnline = false;
  std::string peerUsername, peerOrangeId;
};

struct Msg {
  long long id = 0, chatId = 0, senderId = 0;
  std::string body, attachment, attachmentKind, senderName;
  int64_t createdAt = 0;
  bool mine = false, system = false, deleted = false;
  long long reads = 0;
};

struct Post {
  long long id = 0, authorId = 0, likes = 0, comments = 0, views = 0;
  std::string title, body, media, authorName, authorAvatar, authorUsername, communityName;
  std::vector<std::string> tags;
  int64_t createdAt = 0;
  bool liked = false, isMine = false, isCommunity = false;
};

struct Community {
  long long id = 0, members = 0, chatId = 0;
  std::string name, slug, description, avatar, kind = "group", orangeId, myRole;
  bool isMember = false, isPublic = true;
  std::string ownerName;
};

struct StoryGroup {
  long long userId = 0;
  std::string name, avatar, caption, media, background, kind;
  bool hasUnseen = false;
  long long id = 0, views = 0;
  int64_t createdAt = 0;
  int count = 0;
};

class Api {
public:
  HttpClient http;
  User me;
  bool needTotp = false;
  std::string totpChallenge;

  // ---- сессия ----
  bool loadSession();
  void saveSession(const std::string& token);
  void clearSession();
  std::string sessionPath();

  // ---- авторизация ----
  bool loginPassword(const std::string& identifier, const std::string& password, std::string& err);
  bool loginTotp(const std::string& code, std::string& err);
  bool loginSessionFile(const std::string& fileContent, std::string& rotatedOut, std::string& err);
  bool registerStart(const std::string& method, const std::string& contact, const std::string& username,
                     const std::string& password, std::string& targetOut, std::string& devCode, std::string& err);
  bool registerVerify(const std::string& target, const std::string& code, std::string& err);
  bool loadMe(std::string& err);
  void logout();

  // ---- данные ----
  std::vector<ChatItem> chats(std::string& err, const std::string& query = "");
  std::vector<Msg> messages(long long chatId, std::string& err);
  bool sendMessage(long long chatId, const std::string& body, Msg& out, std::string& err);
  bool markRead(long long chatId, long long messageId);
  bool openDm(const std::string& identifier, long long& chatIdOut, std::string& err);
  std::vector<Post> feed(const std::string& scope, std::string& err, const std::string& query = "");
  bool createPost(const std::string& title, const std::string& body, std::string& err);
  bool likePost(long long postId, bool like, std::string& err);
  bool commentPost(long long postId, const std::string& body, std::string& err);
  std::vector<Community> communities(const std::string& scope, std::string& err, const std::string& query = "");
  bool joinCommunity(long long id, bool join, std::string& err);
  bool followUser(long long id, bool follow, std::string& err);
  std::vector<User> searchUsers(const std::string& query, std::string& err);
  std::vector<StoryGroup> stories(std::string& err);
  bool viewStory(long long id);
  bool createStory(const std::string& caption, const std::string& background, std::string& err);
  bool uploadFile(const std::string& path, const std::string& kind, std::string& urlOut, std::string& err) {
    return http.uploadFile(path, kind, urlOut, err);
  }
  std::string serverUrl() const { return http.baseUrl; }
  void setServerUrl(const std::string& url) { http.baseUrl = url; saveSettings(); }
  bool loadSettings();
  void saveSettings();

  // ---- realtime ----
  bool startRealtime(std::string& err);
  void stopRealtime();
  void realtimeTyping(long long chatId);
  void realtimeRead(long long chatId, long long messageId);
  bool realtimeConnected() const { return ws_.isOpen(); }
  std::function<void(const Json&)> onEvent;  // вызывается из потока WS

  User userFromJson(const Json& j);

private:
  WsClient ws_;
  std::string settingsPath();
};

Msg msgFromJson(const Json& m);
Api& api();

} // namespace om
