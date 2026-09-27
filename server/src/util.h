// OrangeM - small string/encoding/time helpers.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <chrono>
#include <random>
#include <sstream>
#include <openssl/rand.h>

namespace om {

// ---------- time ----------
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
inline std::string isoUtc(int64_t sec) {
  time_t tt = (time_t)sec;
  struct tm g;
  gmtime_r(&tt, &g);
  char buf[40];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
  return buf;
}

// ---------- strings ----------
inline std::string toLower(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return v;
}
inline std::string trim(const std::string& v) {
  size_t a = 0, b = v.size();
  while (a < b && isspace((unsigned char)v[a])) a++;
  while (b > a && isspace((unsigned char)v[b - 1])) b--;
  return v.substr(a, b - a);
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
inline bool endsWith(const std::string& v, const std::string& p) {
  return v.size() >= p.size() && v.compare(v.size() - p.size(), p.size(), p) == 0;
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
inline bool isValidUtf8(const std::string& v) {
  size_t i = 0, n = v.size();
  while (i < n) {
    unsigned char c = v[i];
    int extra = 0;
    if (c < 0x80) extra = 0;
    else if ((c & 0xE0) == 0xC0) extra = 1;
    else if ((c & 0xF0) == 0xE0) extra = 2;
    else if ((c & 0xF8) == 0xF0) extra = 3;
    else return false;
    if (i + extra >= n) return false;
    for (int k = 1; k <= extra; k++)
      if (((unsigned char)v[i + k] & 0xC0) != 0x80) return false;
    i += extra + 1;
  }
  return true;
}

// ---------- hex / base64 ----------
inline std::string toHex(const unsigned char* d, size_t n) {
  static const char* hx = "0123456789abcdef";
  std::string out;
  out.resize(n * 2);
  for (size_t i = 0; i < n; i++) {
    out[i * 2] = hx[d[i] >> 4];
    out[i * 2 + 1] = hx[d[i] & 15];
  }
  return out;
}
inline std::string toHex(const std::string& v) { return toHex((const unsigned char*)v.data(), v.size()); }
inline std::string fromHex(const std::string& v) {
  auto hv = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  for (size_t i = 0; i + 1 < v.size(); i += 2) {
    int a = hv(v[i]), b = hv(v[i + 1]);
    if (a < 0 || b < 0) return {};
    out += (char)((a << 4) | b);
  }
  return out;
}

static const char* B64C = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline std::string base64Encode(const std::string& in) {
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 2 < in.size()) {
    unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
    out += B64C[(v >> 18) & 63]; out += B64C[(v >> 12) & 63];
    out += B64C[(v >> 6) & 63];  out += B64C[v & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    unsigned v = ((unsigned char)in[i] << 16);
    out += B64C[(v >> 18) & 63]; out += B64C[(v >> 12) & 63];
    out += '='; out += '=';
  } else if (i + 2 == in.size()) {
    unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
    out += B64C[(v >> 18) & 63]; out += B64C[(v >> 12) & 63]; out += B64C[(v >> 6) & 63];
    out += '=';
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
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int v = rev[c];
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((buf >> bits) & 0xFF);
    }
  }
  return out;
}
inline std::string base64Url(const std::string& in) {
  std::string s = base64Encode(in);
  s = replaceAll(s, "+", "-");
  s = replaceAll(s, "/", "_");
  s = replaceAll(s, "=", "");
  return s;
}

// ---------- url ----------
inline std::string urlDecode(const std::string& in, bool plusIsSpace = true) {
  std::string out;
  for (size_t i = 0; i < in.size(); i++) {
    char c = in[i];
    if (c == '%' && i + 2 < in.size()) {
      auto hv = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
      };
      int a = hv(in[i + 1]), b = hv(in[i + 2]);
      if (a >= 0 && b >= 0) { out += (char)((a << 4) | b); i += 2; continue; }
    }
    if (plusIsSpace && c == '+') out += ' ';
    else out += c;
  }
  return out;
}
inline std::string urlEncode(const std::string& in) {
  static const char* hx = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : in) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hx[c >> 4]; out += hx[c & 15]; }
  }
  return out;
}

// ---------- random ----------
inline std::string randomBytes(size_t n) {
  std::string out;
  out.resize(n);
  if (RAND_bytes((unsigned char*)out.data(), (int)n) != 1) {
    static std::mt19937_64 rng((uint64_t)nowMs() ^ (uint64_t)(uintptr_t)&out);
    for (size_t i = 0; i < n; i++) out[i] = (char)(rng() & 0xFF);
  }
  return out;
}
inline std::string randomHex(size_t n) { return toHex(randomBytes(n)); }
inline std::string randomToken(size_t nbytes = 32) { return base64Url(randomBytes(nbytes)); }

static const char* B32C = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
inline std::string base32Encode(const std::string& in, bool pad = false) {
  std::string out;
  int buf = 0, bits = 0;
  for (unsigned char c : in) {
    buf = (buf << 8) | c;
    bits += 8;
    while (bits >= 5) { bits -= 5; out += B32C[(buf >> bits) & 31]; }
  }
  if (bits > 0) out += B32C[(buf << (5 - bits)) & 31];
  if (pad) while (out.size() % 8) out += '=';
  return out;
}
inline std::string base32Decode(const std::string& in) {
  int rev[256];
  for (int i = 0; i < 256; i++) rev[i] = -1;
  for (int i = 0; i < 32; i++) rev[(unsigned char)B32C[i]] = i;
  for (int i = 0; i < 26; i++) rev[(unsigned char)('a' + i)] = i;
  std::string out;
  int buf = 0, bits = 0;
  for (unsigned char c : in) {
    if (c == '=' || c == ' ' || c == '-') continue;
    int v = rev[c];
    if (v < 0) continue;
    buf = (buf << 5) | v;
    bits += 5;
    if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
  }
  return out;
}

inline std::string randomDigits(int n) {
  std::string out;
  std::string r = randomBytes(n);
  for (int i = 0; i < n; i++) out += (char)('0' + ((unsigned char)r[i] % 10));
  return out;
}

// Public Orange ID, e.g. "OM-7K2QF3XD"
inline std::string newOrangeId() {
  std::string b = base32Encode(randomBytes(6));
  return "OM-" + b.substr(0, 8);
}

// ---------- validation ----------
inline bool isEmail(const std::string& v) {
  if (v.size() < 6 || v.size() > 190) return false;
  size_t at = v.find('@');
  if (at == std::string::npos || at == 0 || at + 1 >= v.size()) return false;
  if (v.find('@', at + 1) != std::string::npos) return false;
  std::string dom = v.substr(at + 1);
  if (dom.find('.') == std::string::npos) return false;
  for (char c : v)
    if (!(isalnum((unsigned char)c) || c == '@' || c == '.' || c == '_' || c == '-' || c == '+')) return false;
  return true;
}
inline std::string normalizePhone(const std::string& raw) {
  std::string digits;
  for (char c : raw)
    if (isdigit((unsigned char)c)) digits += c;
  if (digits.size() == 11 && digits[0] == '8') digits = "7" + digits.substr(1);
  if (digits.size() == 10) digits = "7" + digits;
  if (digits.size() < 10 || digits.size() > 15) return {};
  return "+" + digits;
}
inline bool isUsername(const std::string& v) {
  if (v.size() < 3 || v.size() > 32) return false;
  for (char c : v)
    if (!(isalnum((unsigned char)c) || c == '_' || c == '.')) return false;
  return true;
}

// ---------- misc ----------
inline std::string mimeFor(const std::string& path) {
  auto low = toLower(path);
  if (endsWith(low, ".html") || endsWith(low, ".htm")) return "text/html; charset=utf-8";
  if (endsWith(low, ".js") || endsWith(low, ".mjs")) return "application/javascript; charset=utf-8";
  if (endsWith(low, ".css")) return "text/css; charset=utf-8";
  if (endsWith(low, ".json")) return "application/json; charset=utf-8";
  if (endsWith(low, ".png")) return "image/png";
  if (endsWith(low, ".jpg") || endsWith(low, ".jpeg")) return "image/jpeg";
  if (endsWith(low, ".gif")) return "image/gif";
  if (endsWith(low, ".webp")) return "image/webp";
  if (endsWith(low, ".svg")) return "image/svg+xml";
  if (endsWith(low, ".ico")) return "image/x-icon";
  if (endsWith(low, ".mp4")) return "video/mp4";
  if (endsWith(low, ".webm")) return "video/webm";
  if (endsWith(low, ".mp3")) return "audio/mpeg";
  if (endsWith(low, ".ogg")) return "audio/ogg";
  if (endsWith(low, ".wav")) return "audio/wav";
  if (endsWith(low, ".woff2")) return "font/woff2";
  if (endsWith(low, ".txt")) return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

inline std::string safeFileName(const std::string& name) {
  std::string out;
  for (char c : name) {
    if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') out += c;
  }
  if (out.empty()) out = "file";
  if (out.size() > 80) out = out.substr(out.size() - 80);
  return out;
}

// Extract #hashtags and @mentions (UTF-8 aware enough for ASCII tags)
inline std::vector<std::string> extractTags(const std::string& text) {
  std::vector<std::string> out;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] != '#') continue;
    size_t j = i + 1;
    std::string tag;
    while (j < text.size() && (isalnum((unsigned char)text[j]) || text[j] == '_' ||
                               (unsigned char)text[j] >= 0x80)) {
      tag += (char)tolower((unsigned char)text[j]);
      j++;
    }
    if (tag.size() >= 2 && tag.size() <= 64) out.push_back(tag);
    i = j;
  }
  return out;
}

inline std::string joinTags(const std::vector<std::string>& tags) {
  std::string out = " ";
  for (auto& t : tags) out += t + " ";
  return out;
}

} // namespace om
