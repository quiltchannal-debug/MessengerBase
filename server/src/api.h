// OrangeM - shared API helpers and route registration.
#pragma once
#include "http.h"
#include "db.h"
#include <string>
#include <vector>

namespace om {

// ---- lookups ----
Row getUserById(long long id);
Row getUserByLogin(const std::string& ident);   // email | phone | username | orange_id | short id
bool userExists(const std::string& username, const std::string& email, const std::string& phone);

// ---- presentation ----
Json userCard(const Row& u, long long viewerId);
Json userSelf(const Row& u, long long viewerId);
Json postJson(const Row& p, long long viewerId);
Json messageJson(const Row& m, long long viewerId);
Json chatJson(const Row& c, long long viewerId);
Json communityJson(const Row& c, long long viewerId);
Json storyJson(const Row& s, long long viewerId);

// ---- auth ----
bool resolveAuth(Request& req);          // fills req.user_id when a valid token is present
bool requireAuth(Request& req, Response& res);
std::string issueSession(long long uid, const std::string& kind, const std::string& ip,
                         const std::string& ua, const std::string& device = "");
void addNotification(long long uid, const std::string& kind, long long actorId,
                     const std::string& entity, long long entityId, const std::string& text);

// ---- relationships ----
bool isContact(long long a, long long b);
bool isBlocked(long long a, long long b);
bool canViewProfile(long long viewerId, const Row& target);
bool canSeeStory(long long viewerId, const Row& owner);
std::vector<long long> chatMemberIds(long long chatId);
bool isChatMember(long long chatId, long long userId, std::string* role = nullptr);
Json presenceFor(const Row& u, long long viewerId);

// ---- delivery ----
void deliverCode(const std::string& channel, const std::string& target, const std::string& purpose,
                 const std::string& code, const std::string& extra = "");
bool rateLimit(const std::string& key, int maxHits, int windowSec);

// ---- route registration ----
void registerAuthRoutes();
void registerUserRoutes();
void registerFeedRoutes();
void registerChatRoutes();
void registerCommunityRoutes();
void registerMiscRoutes();
void registerWsRoutes();

// ---- realtime ----
void wsSendToUser(long long uid, const Json& msg);
void wsSendToChat(long long chatId, const Json& msg, long long exceptUser = 0);
void pushPresence(long long uid, bool online);
void pushNewMessage(const Row& msgRow);

// ---- session files (flash drive key) ----
std::string sessionFileCreate(long long uid, const std::string& label, const std::string& ip);
bool sessionFileLogin(const std::string& fileContent, const std::string& ip, const std::string& ua,
                      long long& userIdOut, std::string& newContentOut, std::string& errOut);
std::string sessionFileRotate(long long userId, const std::string& uuid, const std::string& ip);

// ---- config ----
struct Config {
  std::string dataDir = "data";
  std::string webRoot = "web";
  std::string uploadDir = "data/uploads";
  std::string dbPath = "data/orangem.db";
  std::string host = "0.0.0.0";
  int port = 8080;
  bool devCodes = true;          // expose e-mail/SMS codes to admins for testing
  std::string smtpHost, smtpUser, smtpPass, smtpFrom;
  std::string publicUrl = "http://213.108.1.226";
  std::string adminUser;         // bootstrap admin username
};
Config& config();

} // namespace om
