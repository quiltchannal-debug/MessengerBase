// OrangeM Desktop - WebSocket-клиент (WinSock2, RFC 6455) в отдельном потоке.
#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace om {

class WsClient {
public:
  ~WsClient() { disconnect(); }

  bool connect(const std::string& urlWithQuery, std::string& err);
  void disconnect();
  bool isOpen() const { return open_.load(); }
  bool send(const std::string& text);

  void setOnMessage(std::function<void(const std::string&)> cb) { onMessage_ = std::move(cb); }
  void setOnState(std::function<void(bool)> cb) { onState_ = std::move(cb); }

private:
  void recvLoop();
  bool readExactly(char* buf, size_t n);
  bool sendFrame(const std::string& data, unsigned char opcode);

  intptr_t sock_ = -1;  // SOCKET
  std::atomic<bool> open_{false};
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::mutex sendMtx_;
  std::function<void(const std::string&)> onMessage_;
  std::function<void(bool)> onState_;
};

} // namespace om
