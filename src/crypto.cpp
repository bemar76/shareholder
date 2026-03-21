#include "crypto.h"

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

#include <stdexcept>
#include <cstring>
#include <array>

namespace crypto {

// ---------------------------------------------------------------------------
// Helper: throw on NTSTATUS failure
// ---------------------------------------------------------------------------
static void checkNt(NTSTATUS s, const char* ctx) {
    if (!BCRYPT_SUCCESS(s)) {
        throw std::runtime_error(std::string(ctx) +
            " failed, NTSTATUS=0x" + std::to_string(s));
    }
}

// ---------------------------------------------------------------------------
// Random bytes via BCryptGenRandom
// ---------------------------------------------------------------------------
std::vector<uint8_t> randomBytes(size_t count) {
    std::vector<uint8_t> buf(count);
    checkNt(BCryptGenRandom(nullptr, buf.data(),
                            static_cast<ULONG>(count),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG),
            "BCryptGenRandom");
    return buf;
}

// ---------------------------------------------------------------------------
// PBKDF2-SHA256 key derivation
// ---------------------------------------------------------------------------
std::vector<uint8_t> deriveKey(const std::string& password,
                               const std::vector<uint8_t>& salt) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    checkNt(BCryptOpenAlgorithmProvider(&hAlg,
                                        BCRYPT_SHA256_ALGORITHM,
                                        nullptr,
                                        BCRYPT_ALG_HANDLE_HMAC_FLAG),
            "BCryptOpenAlgorithmProvider(SHA256-HMAC)");

    std::vector<uint8_t> derivedKey(KEY_SIZE);

    // BCryptDeriveKeyPBKDF2 is available since Windows 8
    NTSTATUS st = BCryptDeriveKeyPBKDF2(
        hAlg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        PBKDF2_ITERATIONS,
        derivedKey.data(),
        static_cast<ULONG>(derivedKey.size()),
        0);

    BCryptCloseAlgorithmProvider(hAlg, 0);
    checkNt(st, "BCryptDeriveKeyPBKDF2");

    return derivedKey;
}

// ---------------------------------------------------------------------------
// AES-256-GCM encrypt
// ---------------------------------------------------------------------------
std::vector<uint8_t> encrypt(const std::string& password,
                             const std::vector<uint8_t>& plaintext) {
    auto salt  = randomBytes(SALT_SIZE);
    auto nonce = randomBytes(NONCE_SIZE);
    auto key   = deriveKey(password, salt);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    checkNt(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0),
            "BCryptOpenAlgorithmProvider(AES)");
    checkNt(BCryptSetProperty(hAlg,
                              BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0),
            "BCryptSetProperty(GCM)");

    checkNt(BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                       key.data(),
                                       static_cast<ULONG>(key.size()), 0),
            "BCryptGenerateSymmetricKey");

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    std::vector<uint8_t> tag(TAG_SIZE);
    authInfo.pbNonce   = nonce.data();
    authInfo.cbNonce   = static_cast<ULONG>(nonce.size());
    authInfo.pbTag     = tag.data();
    authInfo.cbTag     = static_cast<ULONG>(tag.size());

    ULONG cipherLen = 0;
    // Query output size
    checkNt(BCryptEncrypt(hKey,
                          const_cast<PUCHAR>(plaintext.data()),
                          static_cast<ULONG>(plaintext.size()),
                          &authInfo, nullptr, 0, nullptr, 0, &cipherLen, 0),
            "BCryptEncrypt(size query)");

    std::vector<uint8_t> ciphertext(cipherLen);
    checkNt(BCryptEncrypt(hKey,
                          const_cast<PUCHAR>(plaintext.data()),
                          static_cast<ULONG>(plaintext.size()),
                          &authInfo, nullptr, 0,
                          ciphertext.data(), cipherLen, &cipherLen, 0),
            "BCryptEncrypt");

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // Assemble blob: magic(4) + version(1) + salt(16) + nonce(12) + tag(16) + ciphertext
    std::vector<uint8_t> blob;
    blob.reserve(5 + SALT_SIZE + NONCE_SIZE + TAG_SIZE + ciphertext.size());
    blob.push_back('S'); blob.push_back('H');
    blob.push_back('L'); blob.push_back('R');
    blob.push_back(0x01); // version
    blob.insert(blob.end(), salt.begin(),       salt.end());
    blob.insert(blob.end(), nonce.begin(),      nonce.end());
    blob.insert(blob.end(), tag.begin(),        tag.end());
    blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());
    return blob;
}

// ---------------------------------------------------------------------------
// AES-256-GCM decrypt
// ---------------------------------------------------------------------------
std::vector<uint8_t> decrypt(const std::string& password,
                             const std::vector<uint8_t>& blob) {
    constexpr size_t HEADER = 5 + SALT_SIZE + NONCE_SIZE + TAG_SIZE;
    if (blob.size() < HEADER)
        throw std::runtime_error("Encrypted file is too small / corrupted");
    if (blob[0] != 'S' || blob[1] != 'H' || blob[2] != 'L' || blob[3] != 'R')
        throw std::runtime_error("Invalid encrypted file format (bad magic)");
    if (blob[4] != 0x01)
        throw std::runtime_error("Unsupported encrypted file version");

    size_t off = 5;
    std::vector<uint8_t> salt (blob.begin() + off, blob.begin() + off + SALT_SIZE);  off += SALT_SIZE;
    std::vector<uint8_t> nonce(blob.begin() + off, blob.begin() + off + NONCE_SIZE); off += NONCE_SIZE;
    std::vector<uint8_t> tag  (blob.begin() + off, blob.begin() + off + TAG_SIZE);   off += TAG_SIZE;
    std::vector<uint8_t> ciphertext(blob.begin() + off, blob.end());

    auto key = deriveKey(password, salt);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    checkNt(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0),
            "BCryptOpenAlgorithmProvider(AES)");
    checkNt(BCryptSetProperty(hAlg,
                              BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0),
            "BCryptSetProperty(GCM)");
    checkNt(BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                       key.data(),
                                       static_cast<ULONG>(key.size()), 0),
            "BCryptGenerateSymmetricKey");

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = nonce.data();
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbTag   = tag.data();
    authInfo.cbTag   = static_cast<ULONG>(tag.size());

    ULONG plainLen = 0;
    checkNt(BCryptDecrypt(hKey,
                          ciphertext.data(),
                          static_cast<ULONG>(ciphertext.size()),
                          &authInfo, nullptr, 0, nullptr, 0, &plainLen, 0),
            "BCryptDecrypt(size query)");

    std::vector<uint8_t> plaintext(plainLen);
    NTSTATUS st = BCryptDecrypt(hKey,
                                ciphertext.data(),
                                static_cast<ULONG>(ciphertext.size()),
                                &authInfo, nullptr, 0,
                                plaintext.data(), plainLen, &plainLen, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (st == STATUS_AUTH_TAG_MISMATCH)
        throw std::runtime_error("Decryption failed: wrong password or corrupted file");
    checkNt(st, "BCryptDecrypt");

    plaintext.resize(plainLen);
    return plaintext;
}

} // namespace crypto
