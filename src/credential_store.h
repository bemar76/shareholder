#pragma once
#include <string>
#include <optional>

namespace credential_store {

constexpr const wchar_t* CREDENTIAL_TARGET = L"Shareholder_ConfigPassword";

/// Store password in Windows Credential Manager (generic credential).
/// Overwrites any existing entry with the same target.
void storePassword(const std::string& password);

/// Retrieve password from Windows Credential Manager.
/// Returns std::nullopt if no credential is stored.
std::optional<std::string> loadPassword();

/// Delete stored credential (if any).
void deletePassword();

/// Prompt user for password via secure console input (no echo).
std::string promptPassword(const std::string& prompt = "Passwort: ");

} // namespace credential_store
