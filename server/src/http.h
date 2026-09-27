// OrangeM - tiny HTTP/1.1 server + WebSocket, thread-per-connection.
#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <atomic>
#include "json.h"

namespace om {

struct Request {
  std::string method;
  std::string path;
  std::string rawQuery;
  std::string body;
  std::map<std::string, std::string> headers;
  std::map<std::string, std::string> query;
  std::map<std::string, std::string> params;
  std::string ip;
  long long user_id = 0;
  std::string token;
  std::string auth_kind;
  bool is_admin = false;
  bool ws = false;
  int wsIndex = -1;

  std::string header(const std::string& k, const std::string& def = "") const {
    auto it = headers.find(k);
    return it == headers.end() ? def : it->second;
  }
  std::string q(const std::string& k, const std::string& def = "") const {
    auto it = query.find(k);
    return it == query.end() ? def : it->second;
  }
  long long qi(const std::string& k, long long def = 0) const {
    auto it = query.find(k);
    if (it == query.end() || it->second.empty()) return def;
    try { return std::stoll(it->second); } catch (...) { return def; }
  }
  Json bodyJson() const {
    if (body.empty()) return Json::obj();
    return Json::parse(body);
  }
  Json okBody() const {
    try { return bodyJson(); } catch (...) { return Json::obj(); }
  }
};

struct Response {
  int status = 200;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  bool headOnly = false;

  void set(const std::string& k, const std::string& v) { headers.push_back({k, v}); }
  void send(int code, const std::string& ct, const std::string& data) {
    status = code;
    set("Content-Type", ct);
    body = data;
  }
  void json(const Json& j, int code = 200) { send(code, "application/json; charset=utf-8", j.dumps()); }
  void ok(const Json& j = Json::obj()) {
    Json r = j;
    r.set("ok", true);
    json(r, 200);
  }
  void fail(int code, const std::string& msg) {
    Json r = Json::obj();
    r.set("ok", false);
    r.set("error", msg);
    json(r, code);
  }
  void redirect(const std::string& loc) {
    status = 302;
    set("Location", loc);
    body = "";
  }
};

using Handler = std::function<void(Request&, Response&)>;

class WsConn : public std::enable_shared_from_this<WsConn> {
public:
  int fd = -1;
  long long user_id = 0;
  std::string token;
  std::atomic<bool> open{true};
  std::function<void(const std::string&)> onMessage;
  std::function<void()> onClose;

  bool sendRaw(const std::string& data, int opcode);
  bool sendText(const std::string& s) { return sendRaw(s, 0x1); }
  bool sendJson(const Json& j) { return sendText(j.dumps()); }
  void closeConn();
  void setSock(int s) { fd = s; }

private:
  std::mutex wmtx_;
};

class WsHub {
public:
  void add(long long uid, const std::shared_ptr<WsConn>& c);
  void remove(long long uid, const std::shared_ptr<WsConn>& c);
  void toUser(long long uid, const Json& msg);
  void toUsers(const std::vector<long long>& uids, const Json& msg);
  void broadcast(const Json& msg);
  size_t onlineCount();
  bool isOnline(long long uid);
  std::vector<long long> onlineUsers();

private:
  std::mutex m_;
  std::map<long long, std::set<std::shared_ptr<WsConn>>> conns_;
};

WsHub& hub();

using WsHandler = std::function<void(Request&, std::shared_ptr<WsConn>)>;

class Router {
public:
  void add(const std::string& method, const std::string& pattern, Handler h);
  void addWs(const std::string& pattern, WsHandler h);
  bool handle(Request& req, Response& res);
  bool hasWs(const std::string& path, Request& req);
  void dispatchWs(Request& req, const std::shared_ptr<WsConn>& conn);

private:
  struct Entry {
    std::string method;
    std::vector<std::string> parts;
    Handler h;
  };
  std::vector<Entry> routes_;
  std::vector<std::pair<std::vector<std::string>, WsHandler>> ws_;
  bool match(const std::vector<std::string>& pat, const std::vector<std::string>& path,
             std::map<std::string, std::string>& params);
};

// Reads one websocket frame; returns false on error. opcode -1 means poll timeout.
bool wsReadFrame(int fd, int& opcode, std::string& payload, int timeoutMs);

class Server {
public:
  Router router;
  std::string webRoot = "web";
  std::string uploadDir = "data/uploads";
  size_t maxBody = 64u * 1024 * 1024;

  bool listenAndServe(const std::string& host, int port);
  void stop();

  // helpers used by handlers
  static void serveStatic(Request& req, Response& res, const std::string& root);
  static bool writeFileAtomic(const std::string& path, const std::string& data);

private:
  void handleConn(int fd, const std::string& ip);
  int listenFd_ = -1;
  std::atomic<bool> running_{false};
};

Router& router();
Server& server();

} // namespace om
