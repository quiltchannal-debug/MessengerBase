#include "http.h"
#include "util.h"
#include "crypto.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <thread>
#include <fstream>
#include <sstream>

namespace om {

static Server g_server;
static Router g_router;
static WsHub g_hub;

Server& server() { return g_server; }
Router& router() { return g_router; }
WsHub& hub() { return g_hub; }

// ------------------------------------------------------------------ Router
void Router::add(const std::string& method, const std::string& pattern, Handler h) {
  Entry e;
  e.method = method;
  e.parts = split(pattern, '/');
  e.h = std::move(h);
  routes_.push_back(std::move(e));
}

void Router::addWs(const std::string& pattern,
                   std::function<void(Request&, std::shared_ptr<WsConn>)> h) {
  ws_.push_back({split(pattern, '/'), std::move(h)});
}

bool Router::match(const std::vector<std::string>& pat, const std::vector<std::string>& path,
                   std::map<std::string, std::string>& params) {
  if (pat.size() != path.size()) return false;
  for (size_t i = 0; i < pat.size(); i++) {
    if (pat[i].empty()) continue;
    if (pat[i][0] == ':') {
      params[pat[i].substr(1)] = urlDecode(path[i]);
    } else if (pat[i][0] == '*') {
      params[pat[i].substr(1)] = urlDecode(path[i]);
    } else if (pat[i] != path[i]) {
      return false;
    }
  }
  return true;
}

bool Router::hasWs(const std::string& path, Request& req) {
  auto parts = split(path, '/');
  for (size_t i = 0; i < ws_.size(); i++) {
    std::map<std::string, std::string> params;
    if (match(ws_[i].first, parts, params)) {
      req.params = params;
      req.ws = true;
      req.wsIndex = (int)i;
      return true;
    }
  }
  return false;
}

void Router::dispatchWs(Request& req, const std::shared_ptr<WsConn>& conn) {
  if (req.wsIndex < 0 || (size_t)req.wsIndex >= ws_.size()) return;
  ws_[(size_t)req.wsIndex].second(req, conn);
}

bool Router::handle(Request& req, Response& res) {
  auto parts = split(req.path, '/');
  for (auto& e : routes_) {
    if (e.method != req.method) continue;
    std::map<std::string, std::string> params;
    if (match(e.parts, parts, params)) {
      req.params = params;
      e.h(req, res);
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------ WsHub
void WsHub::add(long long uid, const std::shared_ptr<WsConn>& c) {
  std::lock_guard<std::mutex> lk(m_);
  conns_[uid].insert(c);
}

void WsHub::remove(long long uid, const std::shared_ptr<WsConn>& c) {
  std::lock_guard<std::mutex> lk(m_);
  auto it = conns_.find(uid);
  if (it == conns_.end()) return;
  it->second.erase(c);
  if (it->second.empty()) conns_.erase(it);
}

void WsHub::toUser(long long uid, const Json& msg) {
  std::vector<std::shared_ptr<WsConn>> targets;
  {
    std::lock_guard<std::mutex> lk(m_);
    auto it = conns_.find(uid);
    if (it != conns_.end()) targets.assign(it->second.begin(), it->second.end());
  }
  std::string payload = msg.dumps();
  for (auto& c : targets)
    if (c->open) c->sendText(payload);
}

void WsHub::toUsers(const std::vector<long long>& uids, const Json& msg) {
  std::string payload = msg.dumps();
  std::vector<std::shared_ptr<WsConn>> targets;
  {
    std::lock_guard<std::mutex> lk(m_);
    for (long long u : uids) {
      auto it = conns_.find(u);
      if (it != conns_.end()) targets.insert(targets.end(), it->second.begin(), it->second.end());
    }
  }
  for (auto& c : targets)
    if (c->open) c->sendText(payload);
}

void WsHub::broadcast(const Json& msg) {
  std::string payload = msg.dumps();
  std::vector<std::shared_ptr<WsConn>> targets;
  {
    std::lock_guard<std::mutex> lk(m_);
    for (auto& kv : conns_)
      targets.insert(targets.end(), kv.second.begin(), kv.second.end());
  }
  for (auto& c : targets)
    if (c->open) c->sendText(payload);
}

size_t WsHub::onlineCount() {
  std::lock_guard<std::mutex> lk(m_);
  return conns_.size();
}

bool WsHub::isOnline(long long uid) {
  std::lock_guard<std::mutex> lk(m_);
  auto it = conns_.find(uid);
  return it != conns_.end() && !it->second.empty();
}

std::vector<long long> WsHub::onlineUsers() {
  std::lock_guard<std::mutex> lk(m_);
  std::vector<long long> out;
  for (auto& kv : conns_) out.push_back(kv.first);
  return out;
}

// ------------------------------------------------------------------ WsConn
bool WsConn::sendRaw(const std::string& data, int opcode) {
  std::lock_guard<std::mutex> lk(wmtx_);
  if (fd < 0 || !open) return false;
  std::string frame;
  frame += (char)(0x80 | opcode);
  size_t n = data.size();
  if (n < 126) {
    frame += (char)n;
  } else if (n < 65536) {
    frame += (char)126;
    frame += (char)((n >> 8) & 0xFF);
    frame += (char)(n & 0xFF);
  } else {
    frame += (char)127;
    for (int i = 7; i >= 0; i--) frame += (char)((n >> (8 * i)) & 0xFF);
  }
  frame += data;
  size_t sent = 0;
  while (sent < frame.size()) {
    ssize_t w = ::send(fd, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
    if (w <= 0) {
      if (errno == EINTR) continue;
      return false;
    }
    sent += (size_t)w;
  }
  return true;
}

void WsConn::closeConn() {
  bool was = open.exchange(false);
  if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
}

// ------------------------------------------------------------------ helpers
static bool sendAll(int fd, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t w = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
    if (w <= 0) {
      if (errno == EINTR) continue;
      return false;
    }
    sent += (size_t)w;
  }
  return true;
}

static std::string statusText(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "OK";
  }
}

static bool readUntil(int fd, std::string& buf, const std::string& delim, size_t limit) {
  char tmp[8192];
  while (buf.find(delim) == std::string::npos) {
    ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n > 0) buf.append(tmp, (size_t)n);
    else if (n == 0) return false;
    else {
      if (errno == EINTR) continue;
      return false;
    }
    if (buf.size() > limit) return false;
  }
  return true;
}

bool Server::writeFileAtomic(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp" + std::to_string(getpid());
  {
    std::ofstream f(tmp, std::ios::binary);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    f.flush();
    if (!f) return false;
  }
  if (::rename(tmp.c_str(), path.c_str()) != 0) {
    ::unlink(tmp.c_str());
    return false;
  }
  return true;
}

void Server::serveStatic(Request& req, Response& res, const std::string& root) {
  std::string rel = req.path;
  if (rel.empty() || rel == "/") rel = "/index.html";
  if (rel.find("..") != std::string::npos) {
    res.fail(400, "bad path");
    return;
  }
  std::string full = root + rel;
  std::ifstream f(full, std::ios::binary);
  if (!f) {
    // SPA fallback for extension-less routes
    if (rel.find('.') == std::string::npos) {
      std::ifstream idx(root + "/index.html", std::ios::binary);
      if (idx) {
        std::stringstream ss;
        ss << idx.rdbuf();
        res.send(200, "text/html; charset=utf-8", ss.str());
        res.set("Cache-Control", "no-cache");
        return;
      }
    }
    res.fail(404, "not found");
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string data = ss.str();
  res.send(200, mimeFor(full), data);
  if (endsWith(full, ".html")) res.set("Cache-Control", "no-cache");
  else res.set("Cache-Control", "public, max-age=300");
}

// ------------------------------------------------------------------ WebSocket framing
bool wsReadFrame(int fd, int& opcode, std::string& payload, int timeoutMs) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  int pr = ::poll(&pfd, 1, timeoutMs);
  if (pr == 0) { opcode = -1; return true; }  // timeout (keepalive tick)
  if (pr < 0) return false;
  unsigned char hdr[2];
  ssize_t n = ::recv(fd, hdr, 2, MSG_WAITALL);
  if (n != 2) return false;
  opcode = hdr[0] & 0x0F;
  bool masked = (hdr[1] & 0x80) != 0;
  uint64_t len = hdr[1] & 0x7F;
  if (len == 126) {
    unsigned char e[2];
    if (::recv(fd, e, 2, MSG_WAITALL) != 2) return false;
    len = ((uint64_t)e[0] << 8) | e[1];
  } else if (len == 127) {
    unsigned char e[8];
    if (::recv(fd, e, 8, MSG_WAITALL) != 8) return false;
    len = 0;
    for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
  }
  if (len > 8u * 1024 * 1024) return false;
  unsigned char mask[4] = {0, 0, 0, 0};
  if (masked && ::recv(fd, mask, 4, MSG_WAITALL) != 4) return false;
  payload.assign(len, 0);
  size_t got = 0;
  while (got < len) {
    ssize_t r = ::recv(fd, &payload[got], (size_t)(len - got), 0);
    if (r <= 0) {
      if (r < 0 && errno == EINTR) continue;
      return false;
    }
    got += (size_t)r;
  }
  if (masked)
    for (size_t i = 0; i < payload.size(); i++) payload[i] ^= (char)mask[i % 4];
  return true;
}

// ------------------------------------------------------------------ Server
bool Server::listenAndServe(const std::string& host, int port) {
  listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0) return false;
  int one = 1;
  setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (host.empty() || host == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
  else inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  if (::bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
    fprintf(stderr, "bind failed on %s:%d: %s\n", host.c_str(), port, strerror(errno));
    return false;
  }
  if (::listen(listenFd_, 256) != 0) return false;
  running_ = true;
  fprintf(stderr, "OrangeM listening on %s:%d\n", host.c_str(), port);

  static std::atomic<int> connCount{0};
  while (running_) {
    sockaddr_in cli{};
    socklen_t clen = sizeof(cli);
    int fd = ::accept(listenFd_, (sockaddr*)&cli, &clen);
    if (fd < 0) {
      if (errno == EINTR) continue;
      if (!running_) break;
      continue;
    }
    if (connCount.load() > 400) {
      const char* busy = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
      ::send(fd, busy, strlen(busy), MSG_NOSIGNAL);
      ::close(fd);
      continue;
    }
    char ipbuf[64];
    inet_ntop(AF_INET, &cli.sin_addr, ipbuf, sizeof(ipbuf));
    connCount++;
    std::thread([this, fd, ip = std::string(ipbuf)]() {
      this->handleConn(fd, ip);
      connCount--;
    }).detach();
  }
  return true;
}

void Server::stop() {
  running_ = false;
  if (listenFd_ >= 0) {
    ::shutdown(listenFd_, SHUT_RDWR);
    ::close(listenFd_);
    listenFd_ = -1;
  }
}

void Server::handleConn(int fd, const std::string& ip) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  timeval tv;
  tv.tv_sec = 30;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  std::string buf;
  bool keepAlive = true;
  while (keepAlive && running_) {
    buf.clear();
    if (!readUntil(fd, buf, "\r\n\r\n", 128 * 1024)) break;
    size_t hdrEnd = buf.find("\r\n\r\n");
    std::string head = buf.substr(0, hdrEnd);
    std::string rest = buf.substr(hdrEnd + 4);

    Request req;
    req.ip = ip;
    std::istringstream hs(head);
    std::string line;
    if (!std::getline(hs, line)) break;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
      auto parts = split(line, ' ');
      if (parts.size() < 3) break;
      req.method = parts[0];
      std::string target = parts[1];
      size_t qpos = target.find('?');
      if (qpos != std::string::npos) {
        req.path = target.substr(0, qpos);
        req.rawQuery = target.substr(qpos + 1);
      } else {
        req.path = target;
      }
    }
    if (!req.path.empty() && req.path[0] != '/') {
      // absolute-form request target
      size_t p = req.path.find("//");
      if (p != std::string::npos) {
        size_t slash = req.path.find('/', p + 2);
        req.path = slash == std::string::npos ? "/" : req.path.substr(slash);
      }
    }
    while (std::getline(hs, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) continue;
      size_t c = line.find(':');
      if (c == std::string::npos) continue;
      std::string k = toLower(trim(line.substr(0, c)));
      std::string v = trim(line.substr(c + 1));
      req.headers[k] = v;
    }
    // query
    for (auto& pair : split(req.rawQuery, '&')) {
      if (pair.empty()) continue;
      size_t eq = pair.find('=');
      if (eq == std::string::npos) req.query[urlDecode(pair)] = "";
      else req.query[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }

    // ---- websocket upgrade ----
    std::string upgrade = toLower(req.header("upgrade"));
    if (upgrade == "websocket" && g_router.hasWs(req.path, req)) {
      std::string key = req.header("sec-websocket-key");
      std::string accept = base64Encode(sha1Raw(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
      std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n";
      if (!sendAll(fd, resp)) break;
      auto conn = std::make_shared<WsConn>();
      conn->setSock(fd);
      g_router.dispatchWs(req, conn);
      ::close(fd);
      return;
    }

    // ---- body ----
    size_t contentLen = 0;
    {
      std::string cl = req.header("content-length");
      if (!cl.empty()) {
        try { contentLen = (size_t)std::stoull(cl); } catch (...) { contentLen = 0; }
      }
    }
    if (contentLen > maxBody) {
      Response r;
      r.set("Server", "OrangeM");
      r.fail(413, "payload too large");
      std::string out = "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\n"
                        "Content-Length: " + std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body;
      sendAll(fd, out);
      break;
    }
    if (contentLen > 0) {
      req.body = rest;
      while (req.body.size() < contentLen) {
        char tmp[16384];
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; break; }
        req.body.append(tmp, (size_t)n);
      }
      if (req.body.size() > contentLen) req.body.resize(contentLen);
    }

    std::string connHdr = toLower(req.header("connection"));
    if (connHdr.find("close") != std::string::npos) keepAlive = false;

    Response res;
    res.set("Server", "OrangeM");
    res.set("Access-Control-Allow-Origin", "*");
    res.set("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Orange-Token");
    res.set("Access-Control-Allow-Methods", "GET, POST, PATCH, PUT, DELETE, OPTIONS");
    if (req.method == "OPTIONS") {
      res.status = 204;
    } else {
      try {
        if (!g_router.handle(req, res)) {
          if (startsWith(req.path, "/api/")) res.fail(404, "endpoint not found");
          else if (req.method == "GET" || req.method == "HEAD") {
            if (req.method == "HEAD") res.headOnly = true;
            serveStatic(req, res, webRoot);
          } else res.fail(404, "not found");
        }
      } catch (const JsonError& e) {
        res.fail(400, std::string("bad json: ") + e.what());
      } catch (const std::exception& e) {
        fprintf(stderr, "handler error: %s\n", e.what());
        res.fail(500, "internal error");
      }
    }

    std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + statusText(res.status) + "\r\n";
    bool hasLen = false, hasCt = false;
    for (auto& h : res.headers) {
      if (toLower(h.first) == "content-length") hasLen = true;
      if (toLower(h.first) == "content-type") hasCt = true;
      out += h.first + ": " + h.second + "\r\n";
    }
    if (!hasCt && !res.body.empty()) out += "Content-Type: application/octet-stream\r\n";
    if (!hasLen) out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    if (!keepAlive) out += "Connection: close\r\n";
    out += "\r\n";
    if (!res.headOnly) out += res.body;
    if (!sendAll(fd, out)) break;
    if (!keepAlive) break;
  }
  ::close(fd);
}

} // namespace om
