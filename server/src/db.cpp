#include "db.h"
#include "util.h"
#include <sqlite3.h>
#include <cstdio>

namespace om {

static Db g_db;

Db& db() { return g_db; }

Db::~Db() { close(); }

void Db::close() {
  if (h_) {
    sqlite3_close(h_);
    h_ = nullptr;
  }
}

bool Db::open(const std::string& path, std::string& err) {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  int rc = sqlite3_open_v2(path.c_str(), &h_,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
  if (rc != SQLITE_OK) {
    err = h_ ? sqlite3_errmsg(h_) : "cannot open database";
    return false;
  }
  sqlite3_busy_timeout(h_, 8000);
  exec("PRAGMA journal_mode=WAL");
  exec("PRAGMA synchronous=NORMAL");
  exec("PRAGMA foreign_keys=ON");
  exec("PRAGMA temp_store=MEMORY");
  initSchema();
  return true;
}

bool Db::exec(const std::string& sql, const std::vector<Json>& params) {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  sqlite3_stmt* st = nullptr;
  const char* tail = nullptr;
  const char* cur = sql.c_str();
  bool ok = true;
  while (*cur) {
    if (sqlite3_prepare_v2(h_, cur, -1, &st, &tail) != SQLITE_OK) {
      lastError_ = sqlite3_errmsg(h_);
      return false;
    }
    if (!st) { cur = tail; if (!cur || !*cur) break; continue; }
    int i = 1;
    for (const auto& p : params) {
      if (p.isNull()) sqlite3_bind_null(st, i);
      else if (p.isNum()) sqlite3_bind_int64(st, i, p.num());
      else if (p.isBool()) sqlite3_bind_int(st, i, p.boolean() ? 1 : 0);
      else {
        const std::string& s = p.str();
        sqlite3_bind_text(st, i, s.data(), (int)s.size(), SQLITE_TRANSIENT);
      }
      i++;
    }
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
      lastError_ = sqlite3_errmsg(h_);
      ok = false;
    }
    sqlite3_finalize(st);
    if (!ok) return false;
    cur = tail;
    if (!cur || !*cur) break;
  }
  return ok;
}

static Json columnValue(sqlite3_stmt* st, int i) {
  switch (sqlite3_column_type(st, i)) {
    case SQLITE_INTEGER: return Json((long long)sqlite3_column_int64(st, i));
    case SQLITE_FLOAT: return Json(sqlite3_column_double(st, i));
    case SQLITE_NULL: return Json();
    case SQLITE_BLOB: {
      const void* p = sqlite3_column_blob(st, i);
      int n = sqlite3_column_bytes(st, i);
      return Json(std::string((const char*)p, n));
    }
    default: {
      const unsigned char* t = sqlite3_column_text(st, i);
      int n = sqlite3_column_bytes(st, i);
      return Json(std::string((const char*)(t ? t : (const unsigned char*)""), n));
    }
  }
}

std::vector<Row> Db::query(const std::string& sql, const std::vector<Json>& params) {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  std::vector<Row> rows;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(h_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
    lastError_ = sqlite3_errmsg(h_);
    return rows;
  }
  int i = 1;
  for (const auto& p : params) {
    if (p.isNull()) sqlite3_bind_null(st, i);
    else if (p.isNum()) sqlite3_bind_int64(st, i, p.num());
    else if (p.isBool()) sqlite3_bind_int(st, i, p.boolean() ? 1 : 0);
    else {
      const std::string& s = p.str();
      sqlite3_bind_text(st, i, s.data(), (int)s.size(), SQLITE_TRANSIENT);
    }
    i++;
  }
  int ncols = sqlite3_column_count(st);
  while (sqlite3_step(st) == SQLITE_ROW) {
    Row r;
    for (int c = 0; c < ncols; c++) r.f[sqlite3_column_name(st, c)] = columnValue(st, c);
    rows.push_back(std::move(r));
  }
  sqlite3_finalize(st);
  return rows;
}

Row Db::queryOne(const std::string& sql, const std::vector<Json>& params) {
  auto rows = query(sql, params);
  return rows.empty() ? Row{} : rows[0];
}

long long Db::insert(const std::string& sql, const std::vector<Json>& params) {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  if (!exec(sql, params)) return 0;
  return (long long)sqlite3_last_insert_rowid(h_);
}

long long Db::count(const std::string& sql, const std::vector<Json>& params) {
  Row r = queryOne(sql, params);
  if (r.f.empty()) return 0;
  return r.f.begin()->second.num();
}

bool Db::tableExists(const std::string& name) {
  Row r = queryOne("SELECT name FROM sqlite_master WHERE type='table' AND name=?", {Json(name)});
  return !r.f.empty();
}

void Db::initSchema() {
  const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  orange_id TEXT UNIQUE NOT NULL,
  short_id INTEGER UNIQUE,
  username TEXT UNIQUE NOT NULL,
  display_name TEXT NOT NULL DEFAULT '',
  bio TEXT NOT NULL DEFAULT '',
  avatar TEXT NOT NULL DEFAULT '',
  banner TEXT NOT NULL DEFAULT '',
  email TEXT UNIQUE,
  email_verified INTEGER NOT NULL DEFAULT 0,
  phone TEXT UNIQUE,
  phone_verified INTEGER NOT NULL DEFAULT 0,
  password_hash TEXT NOT NULL DEFAULT '',
  totp_secret TEXT NOT NULL DEFAULT '',
  totp_enabled INTEGER NOT NULL DEFAULT 0,
  presence TEXT NOT NULL DEFAULT 'online',
  last_seen INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  is_admin INTEGER NOT NULL DEFAULT 0,
  is_banned INTEGER NOT NULL DEFAULT 0,
  pr_dm TEXT NOT NULL DEFAULT 'everyone',
  pr_stories TEXT NOT NULL DEFAULT 'everyone',
  pr_last_seen TEXT NOT NULL DEFAULT 'everyone',
  pr_online TEXT NOT NULL DEFAULT 'everyone',
  pr_read_receipts INTEGER NOT NULL DEFAULT 1,
  pr_find_phone INTEGER NOT NULL DEFAULT 1,
  pr_find_email INTEGER NOT NULL DEFAULT 1,
  pr_profile TEXT NOT NULL DEFAULT 'everyone',
  settings TEXT NOT NULL DEFAULT '{}',
  uid_seq INTEGER NOT NULL DEFAULT 100000
);
CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
CREATE INDEX IF NOT EXISTS idx_users_orange ON users(orange_id);

CREATE TABLE IF NOT EXISTS sessions (
  token TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL,
  kind TEXT NOT NULL DEFAULT 'password',
  created_at INTEGER NOT NULL,
  last_seen INTEGER NOT NULL,
  ip TEXT NOT NULL DEFAULT '',
  ua TEXT NOT NULL DEFAULT '',
  device TEXT NOT NULL DEFAULT '',
  revoked INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);

CREATE TABLE IF NOT EXISTS session_files (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  uuid TEXT UNIQUE NOT NULL,
  label TEXT NOT NULL DEFAULT 'OrangeM Key',
  secret_hash TEXT NOT NULL,
  wrap_key TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  last_used INTEGER NOT NULL DEFAULT 0,
  rotations INTEGER NOT NULL DEFAULT 0,
  revoked INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_sf_user ON session_files(user_id);

CREATE TABLE IF NOT EXISTS auth_codes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  target TEXT NOT NULL,
  channel TEXT NOT NULL,
  purpose TEXT NOT NULL,
  code_hash TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  attempts INTEGER NOT NULL DEFAULT 0,
  consumed INTEGER NOT NULL DEFAULT 0,
  extra TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_codes_target ON auth_codes(target, purpose);

CREATE TABLE IF NOT EXISTS outbox (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  channel TEXT NOT NULL,
  target TEXT NOT NULL,
  subject TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL,
  delivered INTEGER NOT NULL DEFAULT 0,
  error TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS follows (
  follower_id INTEGER NOT NULL,
  followee_id INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (follower_id, followee_id)
);

CREATE TABLE IF NOT EXISTS blocks (
  blocker_id INTEGER NOT NULL,
  blocked_id INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (blocker_id, blocked_id)
);

CREATE TABLE IF NOT EXISTS stories (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  media TEXT NOT NULL DEFAULT '',
  kind TEXT NOT NULL DEFAULT 'image',
  caption TEXT NOT NULL DEFAULT '',
  background TEXT NOT NULL DEFAULT '',
  privacy TEXT NOT NULL DEFAULT 'everyone',
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  deleted INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_stories_user ON stories(user_id, expires_at);

CREATE TABLE IF NOT EXISTS story_views (
  story_id INTEGER NOT NULL,
  viewer_id INTEGER NOT NULL,
  viewed_at INTEGER NOT NULL,
  PRIMARY KEY (story_id, viewer_id)
);

CREATE TABLE IF NOT EXISTS posts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  author_id INTEGER NOT NULL,
  community_id INTEGER,
  title TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  tags TEXT NOT NULL DEFAULT ' ',
  media TEXT NOT NULL DEFAULT '',
  media_kind TEXT NOT NULL DEFAULT '',
  visibility TEXT NOT NULL DEFAULT 'public',
  likes INTEGER NOT NULL DEFAULT 0,
  comments INTEGER NOT NULL DEFAULT 0,
  views INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  edited_at INTEGER NOT NULL DEFAULT 0,
  deleted INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_posts_created ON posts(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_posts_author ON posts(author_id);
CREATE INDEX IF NOT EXISTS idx_posts_community ON posts(community_id);

CREATE TABLE IF NOT EXISTS post_likes (
  post_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (post_id, user_id)
);

CREATE TABLE IF NOT EXISTS post_comments (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  post_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  body TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  deleted INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_comments_post ON post_comments(post_id);

CREATE TABLE IF NOT EXISTS communities (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  orange_id TEXT UNIQUE NOT NULL,
  slug TEXT UNIQUE NOT NULL,
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  avatar TEXT NOT NULL DEFAULT '',
  banner TEXT NOT NULL DEFAULT '',
  kind TEXT NOT NULL DEFAULT 'group',
  is_public INTEGER NOT NULL DEFAULT 1,
  owner_id INTEGER NOT NULL,
  members INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  deleted INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS community_members (
  community_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  role TEXT NOT NULL DEFAULT 'member',
  joined_at INTEGER NOT NULL,
  PRIMARY KEY (community_id, user_id)
);

CREATE TABLE IF NOT EXISTS community_invites (
  code TEXT PRIMARY KEY,
  community_id INTEGER NOT NULL,
  created_by INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL DEFAULT 0,
  max_uses INTEGER NOT NULL DEFAULT 0,
  uses INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS chats (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  kind TEXT NOT NULL DEFAULT 'dm',
  title TEXT NOT NULL DEFAULT '',
  avatar TEXT NOT NULL DEFAULT '',
  description TEXT NOT NULL DEFAULT '',
  community_id INTEGER,
  created_by INTEGER NOT NULL,
  dm_key TEXT UNIQUE,
  created_at INTEGER NOT NULL,
  last_message_at INTEGER NOT NULL DEFAULT 0,
  last_message_id INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_chats_last ON chats(last_message_at DESC);

CREATE TABLE IF NOT EXISTS chat_members (
  chat_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  role TEXT NOT NULL DEFAULT 'member',
  joined_at INTEGER NOT NULL,
  last_read INTEGER NOT NULL DEFAULT 0,
  muted INTEGER NOT NULL DEFAULT 0,
  pinned INTEGER NOT NULL DEFAULT 0,
  left INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (chat_id, user_id)
);
CREATE INDEX IF NOT EXISTS idx_cm_user ON chat_members(user_id);

CREATE TABLE IF NOT EXISTS messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  chat_id INTEGER NOT NULL,
  sender_id INTEGER NOT NULL,
  body TEXT NOT NULL DEFAULT '',
  attachment TEXT NOT NULL DEFAULT '',
  attachment_kind TEXT NOT NULL DEFAULT '',
  reply_to INTEGER NOT NULL DEFAULT 0,
  system INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  edited_at INTEGER NOT NULL DEFAULT 0,
  deleted INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_msg_chat ON messages(chat_id, id DESC);

CREATE TABLE IF NOT EXISTS message_reads (
  message_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  read_at INTEGER NOT NULL,
  PRIMARY KEY (message_id, user_id)
);

CREATE TABLE IF NOT EXISTS reactions (
  message_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  emoji TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (message_id, user_id, emoji)
);

CREATE TABLE IF NOT EXISTS notifications (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  kind TEXT NOT NULL,
  actor_id INTEGER NOT NULL DEFAULT 0,
  entity TEXT NOT NULL DEFAULT '',
  entity_id INTEGER NOT NULL DEFAULT 0,
  text TEXT NOT NULL DEFAULT '',
  read INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_notif_user ON notifications(user_id, read);

CREATE TABLE IF NOT EXISTS file_sessions_audit (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  file_uuid TEXT NOT NULL,
  action TEXT NOT NULL,
  ip TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS meta (
  k TEXT PRIMARY KEY,
  v TEXT NOT NULL
);
)SQL";
  exec(schema);
}

} // namespace om
