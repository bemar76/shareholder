#pragma once
#include <vector>
#include <string>
#include <stdexcept>

// AES-256-GCM encryption/decryption using Windows CNG (bcrypt.dll)
// Key is derived from password using PBKDF2-SHA256.
//
// Encrypted file layout (all big-endian where noted):
//   [4 bytes]  magic "SHLR"
//   [1 byte]   version = 1
//   [16 bytes] PBKDF2 salt
//   [12 bytes] AES-GCM nonce (IV)
//   [16 bytes] AES-GCM authentication tag
//   [N bytes]  ciphertext
//
// Throws std::runtime_error on any failure.

namespace crypto {

constexpr size_t KEY_SIZE   = 32; // AES-256
constexpr size_t SALT_SIZE  = 16;
constexpr size_t NONCE_SIZE = 12;
constexpr size_t TAG_SIZE   = 16;
constexpr uint32_t PBKDF2_ITERATIONS = 200000;

/// Derive a 256-bit key from password + salt using PBKDF2-SHA256.
std::vector<uint8_t> deriveKey(const std::string& password,
                               const std::vector<uint8_t>& salt);

/// Encrypt plaintext with AES-256-GCM. Returns the full encrypted blob.
std::vector<uint8_t> encrypt(const std::string& password,
                             const std::vector<uint8_t>& plaintext);

/// Decrypt an encrypted blob produced by encrypt(). Returns plaintext.
std::vector<uint8_t> decrypt(const std::string& password,
                             const std::vector<uint8_t>& blob);

/// Generates cryptographically secure random bytes.
std::vector<uint8_t> randomBytes(size_t count);

} // namespace crypto
