// OrangeM server entry point.
#include "api.h"
#include "crypto.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fstream>
#include <sys/stat.h>
#include <csignal>

using namespace om;

static std::string env(const char* k, const std::string& def = "") {
  const char* v = getenv(k);
  return v && *v ? std::string(v) : def;
}

static void mkdirp(const std::string& p) { ::mkdir(p.c_str(), 0755); }

static void loadConfig(int argc, char** argv) {
  Config& c = config();
  c.dataDir = env("ORANGEM_DATA", "data");
  c.webRoot = env("ORANGEM_WEB", "web");
  c.dbPath = c.dataDir + "/orangem.db";
  c.uploadDir = c.dataDir + "/uploads";
  c.host = env("ORANGEM_HOST", "0.0.0.0");
  c.port = atoi(env("ORANGEM_PORT", "8080").c_str());
  c.devCodes = env("ORANGEM_DEV_CODES", "1") == "1";
  c.publicUrl = env("ORANGEM_PUBLIC_URL", "http://213.108.1.226");
  c.smtpHost = env("ORANGEM_SMTP_HOST");
  c.smtpUser = env("ORANGEM_SMTP_USER");
  c.smtpPass = env("ORANGEM_SMTP_PASS");
  c.smtpFrom = env("ORANGEM_SMTP_FROM", "orangem@localhost");
  c.adminUser = env("ORANGEM_ADMIN_USER", "admin");
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) c.port = atoi(argv[++i]);
    else if (a == "--host" && i + 1 < argc) c.host = argv[++i];
    else if (a == "--data" && i + 1 < argc) {
      c.dataDir = argv[++i];
      c.dbPath = c.dataDir + "/orangem.db";
      c.uploadDir = c.dataDir + "/uploads";
    } else if (a == "--web" && i + 1 < argc) c.webRoot = argv[++i];
    else if (a == "--no-dev-codes") c.devCodes = false;
    else if (a == "--public-url" && i + 1 < argc) c.publicUrl = argv[++i];
  }
  mkdirp(c.dataDir);
  mkdirp(c.uploadDir);
}

static void bootstrapAdmin() {
  long long users = db().count("SELECT COUNT(*) FROM users");
  if (users > 0) return;
  std::string user = config().adminUser.empty() ? "admin" : config().adminUser;
  std::string pass = env("ORANGEM_ADMIN_PASS");
  bool generated = false;
  if (pass.size() < 8) {
    pass = "OM-" + randomToken(9);
    generated = true;
  }
  std::string orangeId = newOrangeId();
  long long uid = db().insert(
      "INSERT INTO users(orange_id,short_id,username,display_name,password_hash,created_at,last_seen,"
      "is_admin,email_verified,pr_dm,pr_stories) VALUES(?,?,?,?,?,?,?,1,0,'everyone','everyone')",
      {Json(orangeId), Json(100000LL), Json(user), Json("OrangeM Admin"), Json(hashPassword(pass)),
       Json(nowSec()), Json(nowSec())});
  if (uid == 0) {
    fprintf(stderr, "bootstrap admin failed\n");
    return;
  }
  std::string note = "OrangeM admin account\nlogin: " + user + "\npassword: " + pass +
                     "\norange_id: " + orangeId + "\n";
  if (generated) {
    std::ofstream f(config().dataDir + "/ADMIN_CREDENTIALS.txt");
    f << note;
    f.close();
    ::chmod((config().dataDir + "/ADMIN_CREDENTIALS.txt").c_str(), 0600);
  }
  fprintf(stderr, "=== OrangeM bootstrap admin created ===\n%s=================\n", note.c_str());
}

static void maintenanceLoop() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(60));
    try {
      db().exec("DELETE FROM auth_codes WHERE expires_at<?", {Json(nowSec() - 86400)});
      db().exec("UPDATE stories SET deleted=1 WHERE expires_at<?", {Json(nowSec() - 86400)});
      db().exec("DELETE FROM story_views WHERE viewed_at<?", {Json(nowSec() - 7 * 86400)});
      // mark stale users offline
      for (auto& u : db().query("SELECT id FROM users WHERE presence='online'")) {
        long long id = u.num("id");
        if (!hub().isOnline(id)) db().exec("UPDATE users SET presence='offline' WHERE id=?", {Json(id)});
      }
    } catch (...) {}
  }
}

int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  loadConfig(argc, argv);

  std::string err;
  if (!db().open(config().dbPath, err)) {
    fprintf(stderr, "cannot open database %s: %s\n", config().dbPath.c_str(), err.c_str());
    return 1;
  }
  bootstrapAdmin();

  registerAuthRoutes();
  registerUserRoutes();
  registerFeedRoutes();
  registerChatRoutes();
  registerCommunityRoutes();
  registerMiscRoutes();
  registerWsRoutes();

  server().webRoot = config().webRoot;
  server().uploadDir = config().uploadDir;

  std::thread(maintenanceLoop).detach();
  fprintf(stderr, "OrangeM started: data=%s web=%s uploads=%s dev_codes=%d\n", config().dataDir.c_str(),
          config().webRoot.c_str(), config().uploadDir.c_str(), config().devCodes ? 1 : 0);

  if (!server().listenAndServe(config().host, config().port)) return 2;
  return 0;
}
