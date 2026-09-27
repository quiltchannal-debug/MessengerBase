// OrangeM - SQLite wrapper: connection, prepared statements, simple row access.
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <map>
#include "json.h"

struct sqlite3;

namespace om {

class Row {
public:
  std::map<std::string, Json> f;
  const Json& operator[](const std::string& k) const {
    static const Json nullv;
    auto it = f.find(k);
    return it == f.end() ? nullv : it->second;
  }
  bool has(const std::string& k) const { return f.count(k) > 0; }
  std::string str(const std::string& k, const std::string& def = "") const { return (*this)[k].str(def); }
  long long num(const std::string& k, long long def = 0) const { return (*this)[k].num(def); }
  Json json() const {
    Json j = Json::obj();
    for (auto& kv : f) j.o[kv.first] = kv.second;
    return j;
  }
};

class Db {
public:
  Db() {}
  ~Db();
  bool open(const std::string& path, std::string& err);
  void close();

  // Executes SQL with optional params. Returns false on error (msg in lastError).
  bool exec(const std::string& sql, const std::vector<Json>& params = {});

  std::vector<Row> query(const std::string& sql, const std::vector<Json>& params = {});
  Row queryOne(const std::string& sql, const std::vector<Json>& params = {});
  long long insert(const std::string& sql, const std::vector<Json>& params = {});
  long long count(const std::string& sql, const std::vector<Json>& params = {});

  std::string lastError() const { return lastError_; }
  bool tableExists(const std::string& name);

  // RAII transaction on the global recursive lock
  class Tx {
  public:
    explicit Tx(Db& d) : db_(d) { db_.exec("BEGIN IMMEDIATE"); }
    ~Tx() { if (!done_) db_.exec("ROLLBACK"); }
    void commit() { db_.exec("COMMIT"); done_ = true; }
  private:
    Db& db_;
    bool done_ = false;
  };

  std::recursive_mutex& mutex() { return mtx_; }
  void initSchema();

private:
  sqlite3* h_ = nullptr;
  std::string lastError_;
  std::recursive_mutex mtx_;
};

// process-wide database instance
Db& db();

} // namespace om
