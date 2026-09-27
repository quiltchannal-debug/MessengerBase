// OrangeM - cryptography helpers built on OpenSSL EVP.
#pragma once
#include <string>
#include "util.h"

namespace om {

std::string sha256(const std::string& data);
std::string sha1Raw(const std::string& data);
std::string hmacSha1(const std::string& key, const std::string& msg);
std::string hmacSha256(const std::string& key, const std::string& msg);
std::string pbkdf2Sha256(const std::string& password, const std::string& salt, int iters, size_t dklen);
std::string hkdfSha256(const std::string& ikm, const std::string& salt, const std::string& info, size_t len);

// password storage:  pbkdf2$sha256$<iters>$<salt_b64>$<hash_b64>
std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& stored);

// AES-256-GCM. blob = nonce(12) || ciphertext || tag(16)
std::string aesGcmEncrypt(const std::string& key32, const std::string& plaintext, const std::string& aad = "");
bool aesGcmDecrypt(const std::string& key32, const std::string& blob, std::string& out, const std::string& aad = "");

// TOTP (RFC 6238) over base32 secrets
std::string totpCode(const std::string& secretBase32, int64_t t = 0, int step = 30, int digits = 6);
bool totpVerify(const std::string& secretBase32, const std::string& code, int window = 1);
std::string newTotpSecret();

bool constTimeEquals(const std::string& a, const std::string& b);

} // namespace om
