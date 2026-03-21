#pragma once
#include <string>
#include <vector>

// Share entry from config
struct ShareEntry {
    std::string driveLetter;   // e.g. "Z:"
    std::string uncPath;       // e.g. "\\\\nas\\share"
    std::string username;      // optional, empty = use current user
    std::string password;      // optional share-level password
};

// Full application config
struct AppConfig {
    std::vector<ShareEntry> shares;
};

namespace config_manager {

/// Load and decrypt config from file. Throws on error.
AppConfig load(const std::string& filePath, const std::string& password);

/// Encrypt and save config to file. Throws on error.
void save(const std::string& filePath, const std::string& password,
          const AppConfig& config);

/// Returns true if the config file exists.
bool exists(const std::string& filePath);

/// Interactive wizard to create a new config (console I/O).
AppConfig createInteractive();

} // namespace config_manager
