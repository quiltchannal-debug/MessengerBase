#include "crypto.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <cstring>
#include <vector>

namespace om {

std::string sha256(const std::string& data) {
  unsigned char out[SHA256_DIGEST_LENGTH];
  unsigned int len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, data.data(), data.size());
  EVP_DigestFinal_ex(ctx, out, &len);
  EVP_MD_CTX_free(ctx);
  return std::string((char*)out, len);
}

std::string sha1Raw(const std::string& data) {
  unsigned char out[SHA_DIGEST_LENGTH];
  unsigned int len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
  EVP_DigestUpdate(ctx, data.data(), data.size());
  EVP_DigestFinal_ex(ctx, out, &len);
  EVP_MD_CTX_free(ctx);
  return std::string((char*)out, len);
}

static std::string hmac(const EVP_MD* (*md)(), const std::string& key, const std::string& msg) {
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(md(), key.data(), (int)key.size(), (const unsigned char*)msg.data(), msg.size(), out, &len);
  return std::string((char*)out, len);
}

std::string hmacSha1(const std::string& key, const std::string& msg) { return hmac(EVP_sha1, key, msg); }
std::string hmacSha256(const std::string& key, const std::string& msg) { return hmac(EVP_sha256, key, msg); }

std::string pbkdf2Sha256(const std::string& password, const std::string& salt, int iters, size_t dklen) {
  std::vector<unsigned char> out(dklen);
  PKCS5_PBKDF2_HMAC(password.data(), (int)password.size(),
                    (const unsigned char*)salt.data(), (int)salt.size(), iters,
                    EVP_sha256(), (int)dklen, out.data());
  return std::string((char*)out.data(), dklen);
}

std::string hkdfSha256(const std::string& ikm, const std::string& salt, const std::string& info, size_t len) {
  // RFC 5869 extract + expand (used for key derivation, no OpenSSL KDF dependency)
  std::string prk = hmacSha256(salt.empty() ? std::string(32, '\0') : salt, ikm);
  std::string okm;
  std::string t;
  unsigned char counter = 1;
  while (okm.size() < len) {
    t = hmacSha256(prk, t + info + std::string(1, (char)counter));
    okm += t;
    counter++;
  }
  okm.resize(len);
  return okm;
}

std::string hashPassword(const std::string& password) {
  const int iters = 120000;
  std::string salt = randomBytes(16);
  std::string h = pbkdf2Sha256(password, salt, iters, 32);
  return "pbkdf2$sha256$" + std::to_string(iters) + "$" + base64Encode(salt) + "$" + base64Encode(h);
}

bool verifyPassword(const std::string& password, const std::string& stored) {
  auto parts = split(stored, '$');
  if (parts.size() != 5 || parts[0] != "pbkdf2") return false;
  int iters = atoi(parts[2].c_str());
  if (iters <= 0 || iters > 5000000) return false;
  std::string salt = base64Decode(parts[3]);
  std::string want = base64Decode(parts[4]);
  std::string got = pbkdf2Sha256(password, salt, iters, want.size());
  return constTimeEquals(got, want);
}

std::string aesGcmEncrypt(const std::string& key32, const std::string& plaintext, const std::string& aad) {
  if (key32.size() != 32) return {};
  unsigned char nonce[12];
  if (RAND_bytes(nonce, sizeof(nonce)) != 1) return {};
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  std::string out;
  out.resize(plaintext.size() + 16);
  int len = 0, total = 0;
  bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
  ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), nullptr) == 1;
  ok = ok && EVP_EncryptInit_ex(ctx, nullptr, nullptr, (const unsigned char*)key32.data(), nonce) == 1;
  if (ok && !aad.empty())
    ok = EVP_EncryptUpdate(ctx, nullptr, &len, (const unsigned char*)aad.data(), (int)aad.size()) == 1;
  if (ok) {
    ok = EVP_EncryptUpdate(ctx, (unsigned char*)out.data(), &len, (const unsigned char*)plaintext.data(),
                           (int)plaintext.size()) == 1;
    total = len;
  }
  if (ok) {
    int fl = 0;
    ok = EVP_EncryptFinal_ex(ctx, (unsigned char*)out.data() + total, &fl) == 1;
    total += fl;
  }
  unsigned char tag[16];
  if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) return {};
  out.resize(total);
  return std::string((char*)nonce, 12) + out + std::string((char*)tag, 16);
}

bool aesGcmDecrypt(const std::string& key32, const std::string& blob, std::string& out, const std::string& aad) {
  if (key32.size() != 32 || blob.size() < 28) return false;
  const unsigned char* nonce = (const unsigned char*)blob.data();
  size_t ctLen = blob.size() - 12 - 16;
  const unsigned char* ct = nonce + 12;
  const unsigned char* tag = ct + ctLen;
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  out.assign(ctLen, 0);
  int len = 0, total = 0;
  bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
  ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1;
  ok = ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr, (const unsigned char*)key32.data(), nonce) == 1;
  if (ok && !aad.empty())
    ok = EVP_DecryptUpdate(ctx, nullptr, &len, (const unsigned char*)aad.data(), (int)aad.size()) == 1;
  if (ok) {
    ok = EVP_DecryptUpdate(ctx, (unsigned char*)out.data(), &len, ct, (int)ctLen) == 1;
    total = len;
  }
  if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag) == 1;
  if (ok) {
    int fl = 0;
    ok = EVP_DecryptFinal_ex(ctx, (unsigned char*)out.data() + total, &fl) == 1;
    total += fl;
  }
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) return false;
  out.resize(total);
  return true;
}

std::string newTotpSecret() { return base32Encode(randomBytes(20)); }

std::string totpCode(const std::string& secretBase32, int64_t t, int step, int digits) {
  if (t == 0) t = nowSec();
  std::string key = base32Decode(secretBase32);
  if (key.empty()) return "";
  uint64_t counter = (uint64_t)(t / step);
  unsigned char msg[8];
  for (int i = 7; i >= 0; i--) { msg[i] = (unsigned char)(counter & 0xFF); counter >>= 8; }
  std::string mac = hmacSha1(key, std::string((char*)msg, 8));
  if (mac.size() < 20) return "";
  int off = mac[19] & 0x0F;
  uint32_t bin = ((mac[off] & 0x7F) << 24) | ((mac[off + 1] & 0xFF) << 16) |
                 ((mac[off + 2] & 0xFF) << 8) | (mac[off + 3] & 0xFF);
  uint32_t mod = 1;
  for (int i = 0; i < digits; i++) mod *= 10;
  uint32_t v = bin % mod;
  std::string out = std::to_string(v);
  while ((int)out.size() < digits) out = "0" + out;
  return out;
}

bool totpVerify(const std::string& secretBase32, const std::string& code, int window) {
  std::string c = trim(code);
  if (c.size() != 6) return false;
  for (char ch : c)
    if (!isdigit((unsigned char)ch)) return false;
  int64_t now = nowSec();
  for (int w = -window; w <= window; w++) {
    if (totpCode(secretBase32, now + w * 30) == c) return true;
  }
  return false;
}

bool constTimeEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); i++) diff |= (unsigned char)(a[i] ^ b[i]);
  return diff == 0;
}

} // namespace om
