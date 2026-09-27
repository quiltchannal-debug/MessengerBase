// OrangeM Desktop - HTTP-клиент на WinHTTP (синхронный, UTF-8 JSON).
#pragma once
#include <string>
#include <map>
#include "json.h"

namespace om {

struct HttpResponse {
  int status = 0;
  std::string body;
  std::string error;
  bool ok() const { return status >= 200 && status < 300; }
  om::Json json() const {
    try { return om::Json::parse(body); } catch (...) { return om::Json::obj(); }
  }
  std::string errorText() const {
    om::Json j = json();
    std::string e = j["error"].str();
    if (!e.empty()) return e;
    if (!error.empty()) return error;
    return "HTTP " + std::to_string(status);
  }
};

class HttpClient {
public:
  std::string baseUrl = "http://213.108.1.226:8080";  // можно менять в настройках
  std::string token;
  int timeoutMs = 20000;

  HttpResponse request(const std::string& method, const std::string& path,
                       const std::string& body = "", const std::string& contentType = "application/json");
  HttpResponse get(const std::string& path) { return request("GET", path); }
  HttpResponse post(const std::string& path, const om::Json& j) { return request("POST", path, j.dumps()); }
  HttpResponse postRaw(const std::string& path, const std::string& body, const std::string& ct) {
    return request("POST", path, body, ct);
  }
  HttpResponse patch(const std::string& path, const om::Json& j) { return request("PATCH", path, j.dumps()); }
  HttpResponse del(const std::string& path) { return request("DELETE", path); }

  /** Загружает файл на сервер (POST /api/upload) и возвращает его url. */
  bool uploadFile(const std::string& localPath, const std::string& kind, std::string& urlOut, std::string& errOut);

private:
  bool splitUrl(const std::string& url, std::wstring& host, int& port, bool& https, std::wstring& path);
};

} // namespace om
