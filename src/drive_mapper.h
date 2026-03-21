#pragma once
#include "config_manager.h"
#include <string>

namespace drive_mapper {

struct MapResult {
    std::string driveLetter;
    bool        success;
    std::string errorMessage; // empty on success
};

/// Map a single share (non-persistent).
MapResult mapDrive(const ShareEntry& share);

/// Unmap a drive letter. Silent if not mapped.
void unmapDrive(const std::string& driveLetter, bool force = true);

/// Map all shares in config, printing status to stdout.
/// Returns number of successfully mapped drives.
int mapAll(const AppConfig& config);

/// Unmap all drives listed in config.
void unmapAll(const AppConfig& config);

} // namespace drive_mapper
