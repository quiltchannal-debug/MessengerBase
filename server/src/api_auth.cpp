// OrangeM - registration, the four login methods, TOTP and flash-drive session keys.
#include "api.h"
#include "crypto.h"
#include <algorithm>

namespace om {

// ------------------------------------------------------------------ helpers
static std::string codeHash(const std::string& target, const std::string& code) {
  return toHex(sha256("orangem" + target + ":" + code));
}

static void putCode(const std::string& target, const std::string& channel, const std::string& purpose,
                    const std::string& code, const std::string& extra, int ttlSec = 600) {
  db().exec("UPDATE auth_codes SET consumed=1 WHERE target=? AND purpose=? AND consumed=0",
            {Json(target), Json(purpose)});
  db().exec("INSERT INTO auth_codes(target,channel,purpose,code_hash,created_at,expires_at,extra) "
            "VALUES(?,?,?,?,?,?,?)",
            {Json(target), Json(channel), Json(purpose), Json(codeHash(target, code)),
             Json(nowSec()), Json(nowSec() + ttlSec), Json(extra)});
}

static bool checkCode(const std::string& target, const std::string& purpose, const std::string& code) {
  Row r = db().queryOne("SELECT * FROM auth_codes WHERE target=? AND purpose=? AND consumed=0 "
                        "ORDER BY id DESC LIMIT 1",
                        {Json(target), Json(purpose)});
  if (r.f.empty()) return false;
  if (r.num("expires_at") < nowSec()) return false;
  if (r.num("attempts") >= 6) return false;
  db().exec("UPDATE auth_codes SET attempts=attempts+1 WHERE id=?", {Json(r.num("id"))});
  if (codeHash(target, trim(code)) != r.str("code_hash")) return false;
  db().exec("UPDATE auth_codes SET consumed=1 WHERE id=?", {Json(r.num("id"))});
  return true;
}

static Json sessionResponse(long long uid, const std::string& token, const std::string& kind,
                            const std::string& ip, const std::string& ua) {
  Row u = getUserById(uid);
  Json j = Json::obj();
  j.set("ok", true);
  j.set("token", token);
  j.set("kind", kind);
  j.set("user", userSelf(u, uid));
  return j;
}

static long long createUser(const std::string& username, const std::string& displayName,
                            const std::string& passHash, const std::string& email, bool emailVerified,
                            const std::string& phone, bool phoneVerified, std::string& err) {
  if (userExists(username, email, phone)) {
    err = "логин, почта или телефон уже заняты";
    return 0;
  }
  std::string orangeId;
  for (int i = 0; i < 8; i++) {
    orangeId = newOrangeId();
    if (db().count("SELECT COUNT(*) FROM users WHERE orange_id=?", {Json(orangeId)}) == 0) break;
  }
  long long shortId = 100000 + (long long)db().count("SELECT COUNT(*) FROM users") + 1;
  while (db().count("SELECT COUNT(*) FROM users WHERE short_id=?", {Json(shortId)}) > 0) shortId++;
  int64_t now = nowSec();
  long long uid = db().insert(
      "INSERT INTO users(orange_id,short_id,username,display_name,email,email_verified,phone,phone_verified,"
      "password_hash,created_at,last_seen,presence) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
      {Json(orangeId), Json(shortId), Json(username), Json(displayName.empty() ? username : displayName),
       email.empty() ? Json() : Json(email), Json(emailVerified ? 1 : 0),
       phone.empty() ? Json() : Json(phone), Json(phoneVerified ? 1 : 0), Json(passHash),
       Json(now), Json(now), Json("online")});
  if (uid == 0) {
    err = "не удалось создать пользователя";
    return 0;
  }
  return uid;
}

static bool validPassword(const std::string& p) { return p.size() >= 8 && p.size() <= 200; }

// ------------------------------------------------------------------ registration
static void registerStart(Request& req, Response& res) {
  Json b = req.okBody();
  std::string method = b["method"].str("email");
  std::string password = b["password"].str();
  std::string username = trim(b["username"].str());
  std::string displayName = trim(b["display_name"].str());
  std::string email = trim(toLower(b["email"].str()));
  std::string phone = normalizePhone(b["phone"].str());
  std::string target, channel;

  if (method == "email") {
    if (!isEmail(email)) return res.fail(400, "некорректный адрес электронной почты");
    target = email;
    channel = "email";
  } else if (method == "phone") {
    if (phone.empty()) return res.fail(400, "некорректный номер телефона");
    target = phone;
    channel = "phone";
  } else {
    return res.fail(400, "неизвестный метод регистрации");
  }
  if (!isUsername(username)) return res.fail(400, "логин: 3-32 символа (латиница, цифры, _ и .)");
  if (!validPassword(password)) return res.fail(400, "пароль должен быть не короче 8 символов");
  if (!rateLimit("reg:" + req.ip, 12, 3600)) return res.fail(429, "слишком много попыток, попробуйте позже");

  if (userExists(username, email, phone)) return res.fail(409, "логин, почта или телефон уже заняты");

  std::string code = randomDigits(6);
  Json extra = Json::obj();
  extra.set("username", username);
  extra.set("display_name", displayName);
  extra.set("password_hash", hashPassword(password));
  putCode(target, channel, "register", code, extra.dumps(), 900);
  deliverCode(channel, target, "register", code);

  Json r = Json::obj();
  r.set("ok", true);
  r.set("pending", true);
  r.set("target", target);
  r.set("channel", channel);
  r.set("expires_in", 900);
  if (config().devCodes) r.set("dev_code", code);
  res.json(r);
}

static void registerVerify(Request& req, Response& res) {
  Json b = req.okBody();
  std::string target = trim(b["target"].str());
  std::string code = b["code"].str();
  if (target.find('@') != std::string::npos) target = toLower(target);
  else target = normalizePhone(target);
  if (target.empty()) return res.fail(400, "не указана почта или телефон");
  if (!rateLimit("regv:" + req.ip, 20, 900)) return res.fail(429, "слишком много попыток");

  Row row = db().queryOne("SELECT * FROM auth_codes WHERE target=? AND purpose='register' AND consumed=0 "
                          "ORDER BY id DESC LIMIT 1",
                          {Json(target)});
  if (row.f.empty()) return res.fail(400, "код не найден, запросите новый");
  if (!checkCode(target, "register", code)) return res.fail(400, "неверный или истёкший код");

  Json extra;
  try { extra = Json::parse(row.str("extra", "{}")); } catch (...) { return res.fail(500, "повреждённые данные регистрации"); }
  std::string channel = row.str("channel", "email");
  std::string err;
  long long uid = createUser(extra["username"].str(), extra["display_name"].str(),
                             extra["password_hash"].str(),
                             channel == "email" ? target : "", channel == "email",
                             channel == "phone" ? target : "", channel == "phone", err);
  if (!uid) return res.fail(409, err);

  std::string token = issueSession(uid, channel + "_register", req.ip, req.header("user-agent"));
  res.json(sessionResponse(uid, token, channel + "_register", req.ip, req.header("user-agent")));
}

// ------------------------------------------------------------------ password login
static void loginPassword(Request& req, Response& res) {
  Json b = req.okBody();
  std::string ident = b["identifier"].str(b["login"].str());
  std::string password = b["password"].str();
  if (!rateLimit("login:" + req.ip, 20, 300)) return res.fail(429, "слишком много попыток входа");
  if (!rateLimit("loginid:" + toLower(ident), 10, 300)) return res.fail(429, "слишком много попыток входа");

  Row u = getUserByLogin(ident);
  if (u.f.empty()) return res.fail(401, "неверный логин или пароль");
  if (u.str("password_hash").empty()) return res.fail(401, "для этого аккаунта пароль не задан");
  if (!verifyPassword(password, u.str("password_hash"))) return res.fail(401, "неверный логин или пароль");
  if (u.num("is_banned") != 0) return res.fail(403, "аккаунт заблокирован");

  long long uid = u.num("id");
  if (u.num("totp_enabled") != 0) {
    std::string challenge = randomToken(24);
    db().exec("INSERT INTO auth_codes(target,channel,purpose,code_hash,created_at,expires_at) VALUES(?,?,?,?,?,?)",
              {Json(std::to_string(uid)), Json("totp"), Json("challenge"),
               Json(toHex(sha256(challenge))), Json(nowSec()), Json(nowSec() + 300)});
    Json r = Json::obj();
    r.set("ok", true);
    r.set("need_totp", true);
    r.set("challenge", challenge);
    r.set("orange_id", u.str("orange_id"));
    res.json(r);
    return;
  }
  std::string token = issueSession(uid, "password", req.ip, req.header("user-agent"));
  db().exec("UPDATE users SET last_seen=?,presence='online' WHERE id=?", {Json(nowSec()), Json(uid)});
  res.json(sessionResponse(uid, token, "password", req.ip, req.header("user-agent")));
}

static void loginTotp(Request& req, Response& res) {
  Json b = req.okBody();
  std::string challenge = trim(b["challenge"].str());
  std::string code = trim(b["code"].str());
  if (challenge.empty() || code.empty()) return res.fail(400, "нужен challenge и код");
  if (!rateLimit("totp:" + req.ip, 20, 300)) return res.fail(429, "слишком много попыток");
  Row r = db().queryOne("SELECT * FROM auth_codes WHERE purpose='challenge' AND consumed=0 AND code_hash=? "
                        "ORDER BY id DESC LIMIT 1",
                        {Json(toHex(sha256(challenge)))});
  if (r.f.empty()) return res.fail(400, "сессия подтверждения не найдена");
  if (r.num("expires_at") < nowSec()) return res.fail(400, "время подтверждения истекло");
  long long uid = std::stoll(r.str("target", "0"));
  Row u = getUserById(uid);
  if (u.f.empty()) return res.fail(400, "пользователь не найден");
  if (!totpVerify(u.str("totp_secret"), code)) {
    db().exec("UPDATE auth_codes SET attempts=attempts+1 WHERE id=?", {Json(r.num("id"))});
    return res.fail(401, "неверный одноразовый код");
  }
  db().exec("UPDATE auth_codes SET consumed=1 WHERE id=?", {Json(r.num("id"))});
  std::string token = issueSession(uid, "totp", req.ip, req.header("user-agent"));
  db().exec("UPDATE users SET last_seen=?,presence='online' WHERE id=?", {Json(nowSec()), Json(uid)});
  res.json(sessionResponse(uid, token, "totp", req.ip, req.header("user-agent")));
}

// ------------------------------------------------------------------ one-time code login (e-mail / SMS)
static void otpRequest(Request& req, Response& res) {
  Json b = req.okBody();
  std::string ident = trim(b["identifier"].str());
  if (ident.empty()) return res.fail(400, "укажите почту или телефон");
  Row u = getUserByLogin(ident);
  std::string target, channel;
  if (ident.find('@') != std::string::npos) {
    target = toLower(ident);
    channel = "email";
  } else {
    target = normalizePhone(ident);
    channel = "phone";
  }
  if (target.empty()) return res.fail(400, "некорректный адрес или номер");
  // always answer ok (no account enumeration), deliver only when the account exists
  if (!rateLimit("otp:" + target, 5, 600)) return res.fail(429, "код уже отправлен, попробуйте позже");
  std::string code = randomDigits(6);
  bool exists = !u.f.empty();
  if (!exists && channel == "email") {
    u = db().queryOne("SELECT * FROM users WHERE lower(email)=lower(?)", {Json(target)});
    exists = !u.f.empty();
  }
  if (!exists && channel == "phone") {
    u = db().queryOne("SELECT * FROM users WHERE phone=?", {Json(target)});
    exists = !u.f.empty();
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("target", target);
  r.set("channel", channel);
  r.set("expires_in", 600);
  if (exists) {
    putCode(target, channel, "login", code, std::to_string(u.num("id")), 600);
    deliverCode(channel, target, "login", code);
    if (config().devCodes) r.set("dev_code", code);
  } else {
    r.set("delivered", false);
  }
  res.json(r);
}

static void otpVerify(Request& req, Response& res) {
  Json b = req.okBody();
  std::string ident = trim(b["identifier"].str());
  std::string code = trim(b["code"].str());
  std::string target = ident.find('@') != std::string::npos ? toLower(ident) : normalizePhone(ident);
  if (!rateLimit("otpv:" + req.ip, 30, 600)) return res.fail(429, "слишком много попыток");
  Row row = db().queryOne("SELECT * FROM auth_codes WHERE target=? AND purpose='login' AND consumed=0 "
                          "ORDER BY id DESC LIMIT 1",
                          {Json(target)});
  if (row.f.empty() || !checkCode(target, "login", code)) return res.fail(401, "неверный или истёкший код");
  long long uid = std::stoll(row.str("extra", "0"));
  if (uid == 0) return res.fail(400, "код не привязан к аккаунту");
  Row u = getUserById(uid);
  if (u.f.empty()) return res.fail(400, "аккаунт не найден");
  if (u.num("is_banned") != 0) return res.fail(403, "аккаунт заблокирован");
  db().exec("UPDATE users SET email_verified=1 WHERE id=? AND lower(email)=lower(?)", {Json(uid), Json(target)});
  db().exec("UPDATE users SET phone_verified=1 WHERE id=? AND phone=?", {Json(uid), Json(target)});
  std::string token = issueSession(uid, "otp_" + row.str("channel", "email"), req.ip, req.header("user-agent"));
  db().exec("UPDATE users SET last_seen=?,presence='online' WHERE id=?", {Json(nowSec()), Json(uid)});
  res.json(sessionResponse(uid, token, "otp", req.ip, req.header("user-agent")));
}

// ------------------------------------------------------------------ TOTP authenticator app
static void totpSetup(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Row u = getUserById(req.user_id);
  std::string secret = newTotpSecret();
  db().exec("UPDATE users SET totp_secret=? WHERE id=?", {Json(secret), Json(req.user_id)});
  std::string label = u.str("username") + "@OrangeM";
  std::string uri = "otpauth://totp/" + urlEncode(label) + "?secret=" + secret +
                    "&issuer=OrangeM&algorithm=SHA1&digits=6&period=30&orange_id=" + u.str("orange_id");
  Json r = Json::obj();
  r.set("ok", true);
  r.set("secret", secret);
  r.set("formatted", base32Encode(base32Decode(secret)));  // canonical, groups shown client-side
  r.set("uri", uri);
  r.set("orange_id", u.str("orange_id"));
  res.json(r);
}

static void totpEnable(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  Row u = getUserById(req.user_id);
  std::string secret = u.str("totp_secret");
  if (secret.empty()) return res.fail(400, "сначала вызовите /api/auth/totp/setup");
  if (!totpVerify(secret, b["code"].str())) return res.fail(400, "неверный код из приложения");
  db().exec("UPDATE users SET totp_enabled=1 WHERE id=?", {Json(req.user_id)});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("totp_enabled", true);
  res.json(r);
}

static void totpDisable(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  Row u = getUserById(req.user_id);
  std::string secret = u.str("totp_secret");
  if (u.num("totp_enabled") != 0 && !totpVerify(secret, b["code"].str()))
    return res.fail(400, "неверный код из приложения");
  db().exec("UPDATE users SET totp_enabled=0,totp_secret='' WHERE id=?", {Json(req.user_id)});
  Json r = Json::obj();
  r.set("ok", true);
  r.set("totp_enabled", false);
  res.json(r);
}

// ------------------------------------------------------------------ password change / reset
static void passwordChange(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  Row u = getUserById(req.user_id);
  if (!u.str("password_hash").empty() && !verifyPassword(b["old_password"].str(), u.str("password_hash")))
    return res.fail(401, "текущий пароль неверен");
  if (!validPassword(b["new_password"].str())) return res.fail(400, "новый пароль короче 8 символов");
  db().exec("UPDATE users SET password_hash=? WHERE id=?", {Json(hashPassword(b["new_password"].str())), Json(req.user_id)});
  db().exec("UPDATE sessions SET revoked=1 WHERE user_id=? AND token<>?", {Json(req.user_id), Json(req.token)});
  Json r = Json::obj();
  r.set("ok", true);
  res.json(r);
}

static void passwordResetRequest(Request& req, Response& res) {
  Json b = req.okBody();
  std::string ident = trim(b["identifier"].str());
  Row u = getUserByLogin(ident);
  std::string target = ident.find('@') != std::string::npos ? toLower(ident) : normalizePhone(ident);
  std::string channel = ident.find('@') != std::string::npos ? "email" : "phone";
  if (target.empty()) return res.fail(400, "укажите почту или телефон");
  if (!rateLimit("pwr:" + target, 5, 900)) return res.fail(429, "код уже отправлен, попробуйте позже");
  Json r = Json::obj();
  r.set("ok", true);
  r.set("target", target);
  r.set("channel", channel);
  if (!u.f.empty()) {
    std::string code = randomDigits(6);
    putCode(target, channel, "reset", code, std::to_string(u.num("id")), 900);
    deliverCode(channel, target, "reset", code);
    if (config().devCodes) r.set("dev_code", code);
  }
  res.json(r);
}

static void passwordResetVerify(Request& req, Response& res) {
  Json b = req.okBody();
  std::string ident = trim(b["identifier"].str());
  std::string target = ident.find('@') != std::string::npos ? toLower(ident) : normalizePhone(ident);
  if (!validPassword(b["new_password"].str())) return res.fail(400, "пароль короче 8 символов");
  if (!rateLimit("pwrv:" + req.ip, 30, 900)) return res.fail(429, "слишком много попыток");
  Row row = db().queryOne("SELECT * FROM auth_codes WHERE target=? AND purpose='reset' AND consumed=0 "
                          "ORDER BY id DESC LIMIT 1",
                          {Json(target)});
  if (row.f.empty() || !checkCode(target, "reset", b["code"].str()))
    return res.fail(401, "неверный или истёкший код");
  long long uid = std::stoll(row.str("extra", "0"));
  if (!uid) return res.fail(400, "код не привязан к аккаунту");
  db().exec("UPDATE users SET password_hash=? WHERE id=?", {Json(hashPassword(b["new_password"].str())), Json(uid)});
  db().exec("UPDATE sessions SET revoked=1 WHERE user_id=?", {Json(uid)});
  std::string token = issueSession(uid, "password_reset", req.ip, req.header("user-agent"));
  res.json(sessionResponse(uid, token, "password_reset", req.ip, req.header("user-agent")));
}

// ------------------------------------------------------------------ sessions list
static void sessionsList(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json arr = Json::arr();
  for (auto& s : db().query("SELECT * FROM sessions WHERE user_id=? AND revoked=0 ORDER BY last_seen DESC",
                            {Json(req.user_id)})) {
    Json j = Json::obj();
    std::string t = s.str("token");
    j.set("id", toHex(sha256(t)).substr(0, 16));
    j.set("kind", s.str("kind"));
    j.set("created_at", s.num("created_at"));
    j.set("last_seen", s.num("last_seen"));
    j.set("ip", s.str("ip"));
    j.set("device", s.str("device"));
    j.set("ua", s.str("ua").substr(0, 120));
    j.set("current", t == req.token);
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("sessions", arr);
  res.json(r);
}

static void sessionRevoke(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  std::string id = req.params["id"];
  for (auto& s : db().query("SELECT token FROM sessions WHERE user_id=? AND revoked=0", {Json(req.user_id)})) {
    if (toHex(sha256(s.str("token"))).substr(0, 16) == id) {
      db().exec("UPDATE sessions SET revoked=1 WHERE token=?", {Json(s.str("token"))});
      return res.ok();
    }
  }
  res.fail(404, "сессия не найдена");
}

static void logout(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  db().exec("UPDATE sessions SET revoked=1 WHERE token=?", {Json(req.token)});
  res.ok();
}

static void logoutAll(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  db().exec("UPDATE sessions SET revoked=1 WHERE user_id=?", {Json(req.user_id)});
  res.ok();
}

// ------------------------------------------------------------------ flash-drive session key
static const char* KEY_BEGIN = "-----BEGIN ORANGEM SESSION KEY-----";
static const char* KEY_END = "-----END ORANGEM SESSION KEY-----";

struct KeyFile {
  std::string uuid, orangeId, owner;
  int64_t issued = 0, rotations = 0;
  std::string payloadB64;
};

static bool parseKeyFile(const std::string& content, KeyFile& kf) {
  std::string c = content;
  size_t b = c.find(KEY_BEGIN);
  size_t e = c.find(KEY_END);
  if (b == std::string::npos || e == std::string::npos || e < b) return false;
  std::string body = c.substr(b + strlen(KEY_BEGIN), e - b - strlen(KEY_BEGIN));
  for (auto& raw : split(body, '\n')) {
    std::string line = trim(raw);
    if (line.empty()) continue;
    size_t c2 = line.find(':');
    if (c2 == std::string::npos) continue;
    std::string k = toLower(trim(line.substr(0, c2)));
    std::string v = trim(line.substr(c2 + 1));
    if (k == "uuid") kf.uuid = v;
    else if (k == "orange_id") kf.orangeId = v;
    else if (k == "owner") kf.owner = v;
    else if (k == "issued") kf.issued = atoll(v.c_str());
    else if (k == "rotations") kf.rotations = atoll(v.c_str());
    else if (k == "payload") kf.payloadB64 = v;
    else if (k == "data") kf.payloadB64 += v;  // payload may wrap across "data:" lines
  }
  return !kf.uuid.empty() && !kf.payloadB64.empty();
}

static std::string buildKeyFile(const std::string& uuid, const std::string& orangeId,
                                const std::string& owner, int64_t rotations,
                                const std::string& wrapKey, const std::string& secret) {
  Json payload = Json::obj();
  payload.set("uuid", uuid);
  payload.set("orange_id", orangeId);
  payload.set("owner", owner);
  payload.set("secret", secret);
  payload.set("rotation", rotations);
  payload.set("issued", nowSec());
  std::string blob = aesGcmEncrypt(wrapKey, payload.dumps(), "orangem-key-v1");
  std::string b64 = base64Encode(blob);
  std::string out = std::string(KEY_BEGIN) + "\n";
  out += "version: 1\n";
  out += "app: OrangeM\n";
  out += "uuid: " + uuid + "\n";
  out += "orange_id: " + orangeId + "\n";
  out += "owner: " + owner + "\n";
  out += "issued: " + std::to_string(nowSec()) + "\n";
  out += "rotations: " + std::to_string(rotations) + "\n";
  out += "payload: " + b64 + "\n";
  out += std::string(KEY_END) + "\n";
  return out;
}

std::string sessionFileCreate(long long uid, const std::string& label, const std::string& ip) {
  Row u = getUserById(uid);
  if (u.f.empty()) return "";
  std::string uuid = randomHex(16);
  std::string secret = randomToken(32);
  std::string wrapKey = randomBytes(32);
  int64_t now = nowSec();
  db().exec("INSERT INTO session_files(user_id,uuid,label,secret_hash,wrap_key,created_at,rotations) "
            "VALUES(?,?,?,?,?,?,0)",
            {Json(uid), Json(uuid), Json(label.empty() ? "OrangeM Key" : label),
             Json(toHex(sha256(secret))), Json(toHex(wrapKey)), Json(now)});
  db().exec("INSERT INTO file_sessions_audit(user_id,file_uuid,action,ip,created_at) VALUES(?,?,?,?,?)",
            {Json(uid), Json(uuid), Json("create"), Json(ip), Json(now)});
  return buildKeyFile(uuid, u.str("orange_id"), u.str("username"), 0, wrapKey, secret);
}

std::string sessionFileRotate(long long userId, const std::string& uuid, const std::string& ip) {
  Row f = db().queryOne("SELECT * FROM session_files WHERE uuid=? AND user_id=? AND revoked=0",
                        {Json(uuid), Json(userId)});
  if (f.f.empty()) return "";
  Row u = getUserById(userId);
  std::string wrapKey = randomBytes(32);
  std::string secret = randomToken(32);
  int64_t rotations = f.num("rotations") + 1;
  db().exec("UPDATE session_files SET wrap_key=?,secret_hash=?,rotations=?,last_used=? WHERE id=?",
            {Json(toHex(wrapKey)), Json(toHex(sha256(secret))), Json(rotations), Json(nowSec()), Json(f.num("id"))});
  db().exec("INSERT INTO file_sessions_audit(user_id,file_uuid,action,ip,created_at) VALUES(?,?,?,?,?)",
            {Json(userId), Json(uuid), Json("rotate"), Json(ip), Json(nowSec())});
  return buildKeyFile(uuid, u.str("orange_id"), u.str("username"), rotations, wrapKey, secret);
}

bool sessionFileLogin(const std::string& fileContent, const std::string& ip, const std::string& ua,
                      long long& userIdOut, std::string& newContentOut, std::string& errOut) {
  KeyFile kf;
  if (!parseKeyFile(fileContent, kf)) {
    errOut = "файл ключа повреждён или имеет неверный формат";
    return false;
  }
  if (!rateLimit("sf:" + ip, 20, 300)) {
    errOut = "слишком много попыток, подождите";
    return false;
  }
  Row f = db().queryOne("SELECT * FROM session_files WHERE uuid=?", {Json(kf.uuid)});
  if (f.f.empty() || f.num("revoked") != 0) {
    errOut = "ключ не найден или отозван";
    return false;
  }
  std::string wrapKey = fromHex(f.str("wrap_key"));
  std::string plain;
  if (!aesGcmDecrypt(wrapKey, base64Decode(kf.payloadB64), plain, "orangem-key-v1")) {
    errOut = "не удалось расшифровать ключ (повреждён или устарел)";
    return false;
  }
  Json payload;
  try { payload = Json::parse(plain); } catch (...) { errOut = "повреждённое содержимое ключа"; return false; }
  if (toHex(sha256(payload["secret"].str())) != f.str("secret_hash")) {
    errOut = "ключ устарел, используйте последнюю версию файла";
    return false;
  }
  long long uid = f.num("user_id");
  Row u = getUserById(uid);
  if (u.f.empty() || u.num("is_banned") != 0) {
    errOut = "аккаунт недоступен";
    return false;
  }
  userIdOut = uid;
  newContentOut = sessionFileRotate(uid, kf.uuid, ip);
  db().exec("UPDATE session_files SET last_used=? WHERE id=?", {Json(nowSec()), Json(f.num("id"))});
  db().exec("INSERT INTO file_sessions_audit(user_id,file_uuid,action,ip,created_at) VALUES(?,?,?,?,?)",
            {Json(uid), Json(kf.uuid), Json("login"), Json(ip), Json(nowSec())});
  db().exec("UPDATE users SET last_seen=?,presence='online' WHERE id=?", {Json(nowSec()), Json(uid)});
  (void)ua;
  return true;
}

static void sessionFileCreateHandler(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json b = req.okBody();
  std::string content = sessionFileCreate(req.user_id, b["label"].str(), req.ip);
  if (content.empty()) return res.fail(500, "не удалось создать ключ");
  Row u = getUserById(req.user_id);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("file_name", "orangem-" + toLower(u.str("username")) + ".omkey");
  r.set("content", content);
  r.set("hint", "Сохраните файл на флешку. Сервер будет перешифровывать его в реальном времени.");
  res.json(r);
}

static void sessionFileLoginHandler(Request& req, Response& res) {
  Json b = req.okBody();
  std::string content = b["content"].str();
  if (content.empty()) return res.fail(400, "не передан файл ключа");
  long long uid = 0;
  std::string rotated, err;
  if (!sessionFileLogin(content, req.ip, req.header("user-agent"), uid, rotated, err))
    return res.fail(401, err);
  std::string token = issueSession(uid, "session_file", req.ip, req.header("user-agent"),
                                   b["device"].str("Флешка-ключ"));
  Json r = sessionResponse(uid, token, "session_file", req.ip, req.header("user-agent"));
  r.set("rotated_content", rotated);
  Json notice = Json::obj();
  notice.set("type", "session_file.login");
  notice.set("at", nowSec());
  hub().toUser(uid, notice);
  res.json(r);
}

static void sessionFileSync(Request& req, Response& res) {
  Json b = req.okBody();
  std::string content = b["content"].str();
  KeyFile kf;
  if (!parseKeyFile(content, kf)) return res.fail(400, "неверный формат ключа");
  long long uid = 0;
  std::string rotated, err;
  if (!sessionFileLogin(content, req.ip, req.header("user-agent"), uid, rotated, err))
    return res.fail(401, err);
  Json r = Json::obj();
  r.set("ok", true);
  r.set("content", rotated);
  res.json(r);
}

static void sessionFilesList(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json arr = Json::arr();
  for (auto& f : db().query("SELECT * FROM session_files WHERE user_id=? ORDER BY created_at DESC",
                            {Json(req.user_id)})) {
    Json j = Json::obj();
    j.set("id", f.num("id"));
    j.set("uuid", f.str("uuid"));
    j.set("label", f.str("label"));
    j.set("created_at", f.num("created_at"));
    j.set("last_used", f.num("last_used"));
    j.set("rotations", f.num("rotations"));
    j.set("revoked", f.num("revoked") != 0);
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("files", arr);
  res.json(r);
}

static void sessionFileRevoke(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  long long id = atoll(req.params["id"].c_str());
  db().exec("UPDATE session_files SET revoked=1 WHERE id=? AND user_id=?", {Json(id), Json(req.user_id)});
  db().exec("UPDATE sessions SET revoked=1 WHERE user_id=? AND kind='session_file'",
            {Json(req.user_id)});
  res.ok();
}

static void sessionFileLog(Request& req, Response& res) {
  if (!requireAuth(req, res)) return;
  Json arr = Json::arr();
  for (auto& a : db().query("SELECT * FROM file_sessions_audit WHERE user_id=? ORDER BY id DESC LIMIT 50",
                            {Json(req.user_id)})) {
    Json j = Json::obj();
    j.set("action", a.str("action"));
    j.set("uuid", a.str("file_uuid"));
    j.set("ip", a.str("ip"));
    j.set("created_at", a.num("created_at"));
    arr.push(j);
  }
  Json r = Json::obj();
  r.set("ok", true);
  r.set("events", arr);
  res.json(r);
}

static void checkAvailability(Request& req, Response& res) {
  std::string username = trim(req.q("username"));
  std::string email = trim(toLower(req.q("email")));
  std::string phone = normalizePhone(req.q("phone"));
  Json r = Json::obj();
  r.set("ok", true);
  if (!username.empty()) r.set("username_free", !userExists(username, "", ""));
  if (!email.empty()) r.set("email_free", !userExists("", email, ""));
  if (!phone.empty()) r.set("phone_free", !userExists("", "", phone));
  res.json(r);
}

void registerAuthRoutes() {
  auto& r = router();
  r.add("POST", "/api/auth/register", registerStart);
  r.add("POST", "/api/auth/register/verify", registerVerify);
  r.add("POST", "/api/auth/login", loginPassword);
  r.add("POST", "/api/auth/login/totp", loginTotp);
  r.add("POST", "/api/auth/otp/request", otpRequest);
  r.add("POST", "/api/auth/otp/verify", otpVerify);
  r.add("POST", "/api/auth/totp/setup", totpSetup);
  r.add("POST", "/api/auth/totp/enable", totpEnable);
  r.add("POST", "/api/auth/totp/disable", totpDisable);
  r.add("POST", "/api/auth/password/change", passwordChange);
  r.add("POST", "/api/auth/password/reset", passwordResetRequest);
  r.add("POST", "/api/auth/password/reset/verify", passwordResetVerify);
  r.add("GET", "/api/auth/sessions", sessionsList);
  r.add("DELETE", "/api/auth/sessions/:id", sessionRevoke);
  r.add("POST", "/api/auth/logout", logout);
  r.add("POST", "/api/auth/logout/all", logoutAll);
  r.add("GET", "/api/auth/check", checkAvailability);
  // flash-drive key
  r.add("POST", "/api/auth/session-file/create", sessionFileCreateHandler);
  r.add("POST", "/api/auth/session-file/login", sessionFileLoginHandler);
  r.add("POST", "/api/auth/session-file/sync", sessionFileSync);
  r.add("GET", "/api/auth/session-files", sessionFilesList);
  r.add("DELETE", "/api/auth/session-files/:id", sessionFileRevoke);
  r.add("GET", "/api/auth/session-files/log", sessionFileLog);
}

} // namespace om
