#include "http.h"
#include "util.h"
#include <windows.h>
#include <winhttp.h>
#include <fstream>

#pragma comment(lib, "winhttp.lib")

namespace om {

static std::wstring widen(const std::string& s) { return u2w(s); }

bool HttpClient::splitUrl(const std::string& url, std::wstring& host, int& port, bool& https,
                          std::wstring& path) {
  std::wstring wurl = widen(url);
  URL_COMPONENTS uc;
  ZeroMemory(&uc, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  wchar_t hostBuf[256] = {0}, pathBuf[2048] = {0};
  uc.lpszHostName = hostBuf;
  uc.dwHostNameLength = 255;
  uc.lpszUrlPath = pathBuf;
  uc.dwUrlPathLength = 2047;
  if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) return false;
  host = hostBuf;
  path = pathBuf;
  if (uc.lpszExtraInfo && uc.dwExtraInfoLength) {
    path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  }
  https = uc.nScheme == INTERNET_SCHEME_HTTPS;
  port = uc.nPort;
  return true;
}

HttpResponse HttpClient::request(const std::string& method, const std::string& path,
                                 const std::string& body, const std::string& contentType) {
  HttpResponse res;
  std::wstring host, wpath;
  int port = 80;
  bool https = false;
  if (!splitUrl(baseUrl, host, port, https, wpath)) {
    res.error = "Некорректный адрес сервера";
    return res;
  }
  std::wstring wpathFull = wpath;
  if (!path.empty()) {
    if (path[0] == '/') wpathFull += widen(path);
    else wpathFull += widen("/" + path);
  }

  HINTERNET hSession = WinHttpOpen(L"OrangeM-Desktop/1.0",
                                   WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    res.error = "Не удалось инициализировать WinHTTP";
    return res;
  }
  WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
  if (!hConnect) {
    res.error = "Сервер недоступен";
    WinHttpCloseHandle(hSession);
    return res;
  }
  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, widen(method).c_str(), wpathFull.c_str(),
                                          nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    res.error = "Не удалось создать HTTP-запрос";
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
  }

  std::wstring headers = L"Accept: application/json\r\n";
  if (!token.empty()) headers += L"X-Orange-Token: " + widen(token) + L"\r\n";
  if (!body.empty()) headers += L"Content-Type: " + widen(contentType) + L"\r\n";
  headers += L"Connection: keep-alive\r\n";

  BOOL sent = WinHttpSendRequest(
      hRequest, headers.c_str(), (DWORD)-1L,
      body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
      (DWORD)body.size(), 0);
  if (!sent) {
    res.error = "Ошибка отправки запроса (" + std::to_string(GetLastError()) + ")";
  } else if (!WinHttpReceiveResponse(hRequest, nullptr)) {
    res.error = "Нет ответа от сервера";
  } else {
    DWORD statusCode = 0, len = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &len, WINHTTP_NO_HEADER_INDEX);
    res.status = (int)statusCode;
    // Content-Length читаем заранее: иначе WinHttpQueryDataAvailable ждёт данных на keep-alive
    // соединении, хотя тело уже получено целиком.
    long long contentLength = -1;
    {
      wchar_t buf[64] = {0};
      DWORD blen = sizeof(buf);
      if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                              buf, &blen, WINHTTP_NO_HEADER_INDEX)) {
        contentLength = _wtoi64(buf);
      }
    }
    for (;;) {
      if (contentLength >= 0 && (long long)res.body.size() >= contentLength) break;
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
      std::string chunk(avail, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0) break;
      chunk.resize(read);
      res.body += chunk;
      if (res.body.size() > 32u * 1024 * 1024) break;
    }
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return res;
}

bool HttpClient::uploadFile(const std::string& localPath, const std::string& kind,
                            std::string& urlOut, std::string& errOut) {
  std::string data;
  if (!readFileBytes(localPath, data)) {
    errOut = "Не удалось прочитать файл";
    return false;
  }
  if (data.size() > 24u * 1024 * 1024) {
    errOut = "Файл больше 24 МБ";
    return false;
  }
  size_t slash = localPath.find_last_of("\\/");
  std::string name = slash == std::string::npos ? localPath : localPath.substr(slash + 1);
  std::string q = "/api/upload?name=" + urlEncode(name);
  if (!kind.empty()) q += "&kind=" + urlEncode(kind);
  std::string ext = toLower(name.substr(name.find_last_of('.') == std::string::npos
                                            ? name.size()
                                            : name.find_last_of('.')));
  std::string ct = "application/octet-stream";
  if (ext == ".png") ct = "image/png";
  else if (ext == ".jpg" || ext == ".jpeg") ct = "image/jpeg";
  else if (ext == ".gif") ct = "image/gif";
  else if (ext == ".webp") ct = "image/webp";
  else if (ext == ".mp4") ct = "video/mp4";
  else if (ext == ".mp3") ct = "audio/mpeg";
  else if (ext == ".txt") ct = "text/plain";
  HttpResponse r = request("POST", q, data, ct);
  if (!r.ok()) {
    errOut = r.errorText();
    return false;
  }
  urlOut = r.json()["url"].str();
  if (urlOut.empty()) {
    errOut = "Сервер не вернул ссылку на файл";
    return false;
  }
  return true;
}

} // namespace om
