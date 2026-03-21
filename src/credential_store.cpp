#include "credential_store.h"

#include <windows.h>
#include <wincred.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "credui.lib")

#include <stdexcept>
#include <iostream>
#include <string>

namespace credential_store {

// ---------------------------------------------------------------------------
// Store password
// ---------------------------------------------------------------------------
void storePassword(const std::string& password) {
    CREDENTIALW cred = {};
    cred.Type                 = CRED_TYPE_GENERIC;
    cred.TargetName           = const_cast<LPWSTR>(CREDENTIAL_TARGET);
    cred.CredentialBlobSize   = static_cast<DWORD>(password.size());
    cred.CredentialBlob       = reinterpret_cast<LPBYTE>(
                                    const_cast<char*>(password.data()));
    cred.Persist              = CRED_PERSIST_LOCAL_MACHINE;

    if (!CredWriteW(&cred, 0))
        throw std::runtime_error("CredWrite failed: " +
                                 std::to_string(GetLastError()));
}

// ---------------------------------------------------------------------------
// Load password
// ---------------------------------------------------------------------------
std::optional<std::string> loadPassword() {
    PCREDENTIALW pCred = nullptr;
    if (!CredReadW(CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0, &pCred)) {
        DWORD err = GetLastError();
        if (err == ERROR_NOT_FOUND)
            return std::nullopt;
        throw std::runtime_error("CredRead failed: " + std::to_string(err));
    }

    std::string password(
        reinterpret_cast<char*>(pCred->CredentialBlob),
        pCred->CredentialBlobSize);
    CredFree(pCred);
    return password;
}

// ---------------------------------------------------------------------------
// Delete credential
// ---------------------------------------------------------------------------
void deletePassword() {
    if (!CredDeleteW(CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0)) {
        DWORD err = GetLastError();
        if (err != ERROR_NOT_FOUND)
            throw std::runtime_error("CredDelete failed: " +
                                     std::to_string(err));
    }
}

// ---------------------------------------------------------------------------
// Prompt password from console without echo
// ---------------------------------------------------------------------------
std::string promptPassword(const std::string& prompt) {
    std::cout << prompt << std::flush;

    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD  mode   = 0;
    GetConsoleMode(hStdin, &mode);
    // Disable echo
    SetConsoleMode(hStdin, mode & ~ENABLE_ECHO_INPUT);

    std::string password;
    std::getline(std::cin, password);

    // Restore
    SetConsoleMode(hStdin, mode);
    std::cout << "\n";
    return password;
}

} // namespace credential_store
