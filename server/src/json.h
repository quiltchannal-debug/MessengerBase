// OrangeM - minimal but complete JSON value / parser / serializer.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <initializer_list>
#include <cstring>
#include <cctype>

namespace om {

struct JsonError : std::runtime_error {
  explicit JsonError(const std::string& m) : std::runtime_error(m) {}
};

class Json {
public:
  enum class T { Null, Bool, Num, Str, Arr, Obj };

  T t = T::Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::map<std::string, Json> o;

  Json() {}
  Json(std::nullptr_t) {}
  Json(bool v) : t(T::Bool), b(v) {}
  Json(int v) : t(T::Num), n(v) {}
  Json(long v) : t(T::Num), n((double)v) {}
  Json(long long v) : t(T::Num), n((double)v) {}
  Json(double v) : t(T::Num), n(v) {}
  Json(const char* v) : t(T::Str), s(v ? v : "") {}
  Json(const std::string& v) : t(T::Str), s(v) {}

  static Json arr() { Json j; j.t = T::Arr; return j; }
  static Json obj() { Json j; j.t = T::Obj; return j; }

  bool isNull() const { return t == T::Null; }
  bool isBool() const { return t == T::Bool; }
  bool isNum() const { return t == T::Num; }
  bool isStr() const { return t == T::Str; }
  bool isArr() const { return t == T::Arr; }
  bool isObj() const { return t == T::Obj; }

  // ---- readers (never throw) ----
  std::string str(const std::string& def = "") const { return t == T::Str ? s : def; }
  long long num(long long def = 0) const {
    if (t == T::Num) return (long long)llround(n);
    if (t == T::Bool) return b ? 1 : 0;
    if (t == T::Str) { try { return std::stoll(s); } catch (...) { return def; } }
    return def;
  }
  double dbl(double def = 0) const {
    if (t == T::Num) return n;
    if (t == T::Str) { try { return std::stod(s); } catch (...) { return def; } }
    return def;
  }
  bool boolean(bool def = false) const {
    if (t == T::Bool) return b;
    if (t == T::Num) return n != 0;
    if (t == T::Str) return s == "true" || s == "1";
    return def;
  }
  size_t size() const { return t == T::Arr ? a.size() : (t == T::Obj ? o.size() : 0); }

  bool has(const std::string& k) const { return t == T::Obj && o.count(k) > 0; }

  const Json& operator[](const std::string& k) const {
    static const Json nullv;
    if (t != T::Obj) return nullv;
    auto it = o.find(k);
    return it == o.end() ? nullv : it->second;
  }
  Json& operator[](const std::string& k) {
    if (t != T::Obj) { t = T::Obj; }
    return o[k];
  }
  Json& at(size_t i) { return a.at(i); }
  const Json& at(size_t i) const { return a.at(i); }

  void push(Json v) { if (t != T::Arr) t = T::Arr; a.push_back(std::move(v)); }
  Json& set(const std::string& k, Json v) { if (t != T::Obj) t = T::Obj; o[k] = std::move(v); return *this; }
  void remove(const std::string& k) { o.erase(k); }

  // ---- serializer ----
  std::string dumps() const {
    std::string out;
    out.reserve(256);
    write(out);
    return out;
  }

  void write(std::string& out) const {
    switch (t) {
      case T::Null: out += "null"; break;
      case T::Bool: out += b ? "true" : "false"; break;
      case T::Num: {
        if (std::isnan(n) || std::isinf(n)) { out += "0"; break; }
        if (n == (double)(long long)n && std::fabs(n) < 9.0e15) {
          char buf[32];
          snprintf(buf, sizeof(buf), "%lld", (long long)n);
          out += buf;
        } else {
          char buf[40];
          snprintf(buf, sizeof(buf), "%.10g", n);
          out += buf;
        }
        break;
      }
      case T::Str: writeString(out, s); break;
      case T::Arr: {
        out += '[';
        bool first = true;
        for (auto& e : a) { if (!first) out += ','; first = false; e.write(out); }
        out += ']';
        break;
      }
      case T::Obj: {
        out += '{';
        bool first = true;
        for (auto& kv : o) {
          if (!first) out += ',';
          first = false;
          writeString(out, kv.first);
          out += ':';
          kv.second.write(out);
        }
        out += '}';
        break;
      }
    }
  }

  static void writeString(std::string& out, const std::string& v) {
    out += '"';
    for (unsigned char c : v) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
          } else out += (char)c;
      }
    }
    out += '"';
  }

  // ---- parser ----
  static Json parse(const std::string& src) {
    Parser p(src);
    p.ws();
    Json v = p.value();
    p.ws();
    if (p.i != p.n) throw JsonError("trailing data at " + std::to_string(p.i));
    return v;
  }

private:
  struct Parser {
    const std::string& s;
    size_t i = 0, n;
    explicit Parser(const std::string& src) : s(src), n(src.size()) {}

    void ws() {
      while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i++;
        else break;
      }
    }
    char peek() { if (i >= n) throw JsonError("unexpected end"); return s[i]; }
    void expect(char c) { if (i >= n || s[i] != c) throw JsonError(std::string("expected '") + c + "'"); i++; }

    Json value() {
      if (i >= n) throw JsonError("empty");
      char c = s[i];
      switch (c) {
        case '{': return object();
        case '[': return array();
        case '"': { Json j; j.t = T::Str; j.s = string(); return j; }
        case 't': lit("true"); return Json(true);
        case 'f': lit("false"); return Json(false);
        case 'n': lit("null"); return Json();
        default: return number();
      }
    }
    void lit(const char* l) {
      size_t len = strlen(l);
      if (s.compare(i, len, l) != 0) throw JsonError("bad literal");
      i += len;
    }
    Json object() {
      Json j = Json::obj();
      expect('{');
      ws();
      if (i < n && s[i] == '}') { i++; return j; }
      while (true) {
        ws();
        if (peek() != '"') throw JsonError("expected key");
        std::string k = string();
        ws();
        expect(':');
        ws();
        j.o[k] = value();
        ws();
        char c = peek();
        if (c == ',') { i++; continue; }
        if (c == '}') { i++; break; }
        throw JsonError("expected , or }");
      }
      return j;
    }
    Json array() {
      Json j = Json::arr();
      expect('[');
      ws();
      if (i < n && s[i] == ']') { i++; return j; }
      while (true) {
        ws();
        j.a.push_back(value());
        ws();
        char c = peek();
        if (c == ',') { i++; continue; }
        if (c == ']') { i++; break; }
        throw JsonError("expected , or ]");
      }
      return j;
    }
    std::string string() {
      expect('"');
      std::string out;
      while (true) {
        if (i >= n) throw JsonError("unterminated string");
        char c = s[i++];
        if (c == '"') break;
        if (c == '\\') {
          if (i >= n) throw JsonError("bad escape");
          char e = s[i++];
          switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
              if (i + 4 > n) throw JsonError("bad \\u");
              unsigned cp = (unsigned)strtol(s.substr(i, 4).c_str(), nullptr, 16);
              i += 4;
              if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= n && s[i] == '\\' && s[i + 1] == 'u') {
                unsigned lo = (unsigned)strtol(s.substr(i + 2, 4).c_str(), nullptr, 16);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                  i += 6;
                  cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
              }
              encodeUtf8(out, cp);
              break;
            }
            default: throw JsonError("bad escape char");
          }
        } else out += c;
      }
      return out;
    }
    static void encodeUtf8(std::string& out, unsigned cp) {
      if (cp < 0x80) out += (char)cp;
      else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
      } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
      }
    }
    Json number() {
      size_t start = i;
      if (i < n && (s[i] == '-' || s[i] == '+')) i++;
      bool any = false;
      while (i < n && isdigit((unsigned char)s[i])) { i++; any = true; }
      if (i < n && s[i] == '.') { i++; while (i < n && isdigit((unsigned char)s[i])) { i++; any = true; } }
      if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '-' || s[i] == '+')) i++;
        while (i < n && isdigit((unsigned char)s[i])) i++;
      }
      if (!any) throw JsonError("bad number");
      Json j;
      j.t = T::Num;
      j.n = strtod(s.substr(start, i - start).c_str(), nullptr);
      return j;
    }
  };
};

} // namespace om
