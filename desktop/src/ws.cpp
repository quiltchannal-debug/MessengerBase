#include "ws.h"
#include "util.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace om {

static std::string randomKey() {
  std::string raw;
  for (int i = 0; i < 16; i++) {
    unsigned int v = (unsigned int)(rand() ^ (GetTickCount64() >> (i % 7)) ^ (i * 2654435761u));
    raw += (char)(v & 0xFF);
  }
  return base64Encode(raw);
}

bool WsClient::connect(const std::string& urlWithQuery, std::string& err) {
  disconnect();
  std::string url = urlWithQuery;
  bool secure = false;
  if (startsWith(url, "wss://")) { secure = true; url = url.substr(6); }
  else if (startsWith(url, "ws://")) { url = url.substr(5); }

  std::string hostPort = url, path = "/";
  size_t slash = url.find('/');
  if (slash != std::string::npos) {
    hostPort = url.substr(0, slash);
    path = url.substr(slash);
  }
  std::string host = hostPort;
  int port = secure ? 443 : 80;
  size_t colon = hostPort.find(':');
  if (colon != std::string::npos) {
    host = hostPort.substr(0, colon);
    port = atoi(hostPort.substr(colon + 1).c_str());
  }
  if (secure) {
    err = "Защищённое соединение wss:// требует TLS (используйте http-адрес сервера)";
    return false;
  }

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    err = "Ошибка инициализации Winsock";
    return false;
  }

  addrinfo hints;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* result = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result) {
    err = "Не удалось разрешить адрес " + host;
    WSACleanup();
    return false;
  }
  SOCKET s = INVALID_SOCKET;
  for (addrinfo* p = result; p; p = p->ai_next) {
    s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (::connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) break;
    closesocket(s);
    s = INVALID_SOCKET;
  }
  freeaddrinfo(result);
  if (s == INVALID_SOCKET) {
    err = "Сервер " + host + ":" + std::to_string(port) + " недоступен";
    WSACleanup();
    return false;
  }
  sock_ = (intptr_t)s;

  std::string key = randomKey();
  std::string req = "GET " + path + " HTTP/1.1\r\n" +
                    "Host: " + host + ":" + std::to_string(port) + "\r\n" +
                    "Upgrade: websocket\r\nConnection: Upgrade\r\n" +
                    "Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n" +
                    "User-Agent: OrangeM-Desktop/1.0\r\n\r\n";
  if (::send(s, req.data(), (int)req.size(), 0) <= 0) {
    err = "Не удалось отправить рукопожатие";
    disconnect();
    return false;
  }

  // читаем заголовки ответа по байтам до \r\n\r\n
  std::string resp;
  char c;
  while (resp.size() < 8192) {
    int n = ::recv(s, &c, 1, 0);
    if (n <= 0) break;
    resp += c;
    if (resp.size() >= 4 && resp.compare(resp.size() - 4, 4, "\r\n\r\n") == 0) break;
  }
  if (resp.find("101") == std::string::npos) {
    err = "Сервер отклонил websocket-соединение";
    disconnect();
    return false;
  }
  std::string expect = base64Encode(sha1Raw(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
  if (resp.find(expect) == std::string::npos) {
    err = "Неверный ответ websocket-рукопожатия";
    disconnect();
    return false;
  }

  open_ = true;
  stop_ = false;
  if (onState_) onState_(true);
  thread_ = std::thread([this] { recvLoop(); });
  return true;
}

void WsClient::disconnect() {
  stop_ = true;
  if (sock_ >= 0) {
    ::shutdown((SOCKET)sock_, SD_BOTH);
    ::closesocket((SOCKET)sock_);
    sock_ = -1;
  }
  if (thread_.joinable()) thread_.join();
  if (open_.exchange(false) && onState_) onState_(false);
  WSACleanup();
}

bool WsClient::readExactly(char* buf, size_t n) {
  size_t got = 0;
  while (got < n) {
    int r = ::recv((SOCKET)sock_, buf + got, (int)(n - got), 0);
    if (r <= 0) return false;
    got += (size_t)r;
  }
  return true;
}

void WsClient::recvLoop() {
  std::string pending;
  while (!stop_) {
    char hdr[2];
    if (!readExactly(hdr, 2)) break;
    unsigned char opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;
    if (len == 126) {
      unsigned char e[2];
      if (!readExactly((char*)e, 2)) break;
      len = ((uint64_t)e[0] << 8) | e[1];
    } else if (len == 127) {
      unsigned char e[8];
      if (!readExactly((char*)e, 8)) break;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    if (len > 8u * 1024 * 1024) break;
    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked && !readExactly((char*)mask, 4)) break;
    std::string payload(len, '\0');
    if (len && !readExactly(&payload[0], (size_t)len)) break;
    if (masked)
      for (size_t i = 0; i < payload.size(); i++) payload[i] ^= (char)mask[i % 4];

    if (opcode == 0x1 || opcode == 0x0) {
      pending += payload;
      if (onMessage_) onMessage_(pending);
      pending.clear();
    } else if (opcode == 0x8) {
      break;
    } else if (opcode == 0x9) {
      sendFrame(payload, 0xA);
    }
    // 0xA (pong) игнорируем
  }
  open_ = false;
  if (onState_) onState_(false);
}

bool WsClient::sendFrame(const std::string& data, unsigned char opcode) {
  if (!open_.load() || sock_ < 0) return false;
  std::lock_guard<std::mutex> lk(sendMtx_);
  std::string frame;
  frame += (char)(0x80 | opcode);
  size_t n = data.size();
  if (n < 126) frame += (char)(0x80 | n);
  else if (n < 65536) {
    frame += (char)(0x80 | 126);
    frame += (char)((n >> 8) & 0xFF);
    frame += (char)(n & 0xFF);
  } else {
    frame += (char)(0x80 | 127);
    for (int i = 7; i >= 0; i--) frame += (char)((n >> (8 * i)) & 0xFF);
  }
  unsigned char mask[4];
  for (int i = 0; i < 4; i++) mask[i] = (unsigned char)(rand() & 0xFF);
  frame.append((char*)mask, 4);
  size_t start = frame.size();
  frame += data;
  for (size_t i = 0; i < n; i++) frame[start + i] ^= (char)mask[i % 4];
  size_t sent = 0;
  while (sent < frame.size()) {
    int w = ::send((SOCKET)sock_, frame.data() + sent, (int)(frame.size() - sent), 0);
    if (w <= 0) return false;
    sent += (size_t)w;
  }
  return true;
}

bool WsClient::send(const std::string& text) { return sendFrame(text, 0x1); }

} // namespace om
