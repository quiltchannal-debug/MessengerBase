// OrangeM Desktop - Windows helpers: UTF-8/UTF-16, base64, SHA-1, время, URL.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <cstdio>

namespace om {

// ---------- UTF ----------
inline std::wstring u2w(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
  return out;
}
inline std::string w2u(const std::wstring& s) {
  if (s.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string out((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
  return out;
}

// ---------- время ----------
inline int64_t nowSec() {
  return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
inline int64_t nowMs() {
  return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// ---------- строки ----------
inline std::string trim(const std::string& v) {
  size_t a = 0, b = v.size();
  while (a < b && isspace((unsigned char)v[a])) a++;
  while (b > a && isspace((unsigned char)v[b - 1])) b--;
  return v.substr(a, b - a);
}
inline std::string toLower(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)tolower(c); });
  return v;
}
inline std::vector<std::string> split(const std::string& v, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : v) {
    if (c == sep) { out.push_back(cur); cur.clear(); }
    else cur += c;
  }
  out.push_back(cur);
  return out;
}
inline bool startsWith(const std::string& v, const std::string& p) {
  return v.size() >= p.size() && v.compare(0, p.size(), p) == 0;
}
inline std::string replaceAll(std::string v, const std::string& from, const std::string& to) {
  if (from.empty()) return v;
  size_t pos = 0;
  while ((pos = v.find(from, pos)) != std::string::npos) {
    v.replace(pos, from.size(), to);
    pos += to.size();
  }
  return v;
}

// ---------- base64 ----------
static const char* B64C = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline std::string base64Encode(const std::string& in) {
  std::string out;
  size_t i = 0;
  while (i + 2 < in.size()) {
    unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
    out += B64C[(v >> 18) & 63]; out += B64C[(v >> 12) & 63];
    out += B64C[(v >> 6) & 63];  out += B64C[v & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    unsigned v = ((unsigned char)in[i] << 16);
    out += B64C[(v >> 18) & 63]; out += B64C[(v >> 12) & 63]; out += "==";
  } else if (i + 2 == in.size()) {
    unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
    out += B64C[(v >> 18) & 63]; out += B64C[(v >> 12) & 63]; out += B64C[(v >> 6) & 63]; out += '=';
  }
  return out;
}
inline std::string base64Decode(const std::string& in) {
  int rev[256];
  for (int i = 0; i < 256; i++) rev[i] = -1;
  for (int i = 0; i < 64; i++) rev[(unsigned char)B64C[i]] = i;
  std::string out;
  int buf = 0, bits = 0;
  for (unsigned char c : in) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    int v = rev[c];
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
  }
  return out;
}

// ---------- SHA-1 (для рукопожатия WebSocket) ----------
inline std::string sha1Raw(const std::string& data) {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  std::string msg = data;
  uint64_t ml = (uint64_t)data.size() * 8;
  msg += (char)0x80;
  while (msg.size() % 64 != 56) msg += (char)0;
  for (int i = 7; i >= 0; i--) msg += (char)((ml >> (8 * i)) & 0xFF);
  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      w[i] = ((uint32_t)(unsigned char)msg[chunk + i * 4] << 24) |
             ((uint32_t)(unsigned char)msg[chunk + i * 4 + 1] << 16) |
             ((uint32_t)(unsigned char)msg[chunk + i * 4 + 2] << 8) |
             ((uint32_t)(unsigned char)msg[chunk + i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++) {
      uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
      w[i] = (v << 1) | (v >> 31);
    }
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t tmp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
      e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = tmp;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
  }
  std::string out;
  uint32_t hs[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; i++)
    for (int b = 3; b >= 0; b--) out += (char)((hs[i] >> (8 * b)) & 0xFF);
  return out;
}

// ---------- URL / UUID ----------
inline std::string urlEncode(const std::string& in) {
  static const char* hx = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : in) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hx[c >> 4]; out += hx[c & 15]; }
  }
  return out;
}
inline std::string urlDecode(const std::string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '%' && i + 2 < in.size()) {
      auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int a = hv(in[i + 1]), b = hv(in[i + 2]);
      if (a >= 0 && b >= 0) { out += (char)((a << 4) | b); i += 2; continue; }
    }
    out += in[i];
  }
  return out;
}

// ---------- файлы ----------
std::string appDataDir();
bool readFileBytes(const std::string& path, std::string& out);
bool writeFileBytes(const std::string& path, const std::string& data);

} // namespace om
