#include "drive_mapper.h"

#include <windows.h>
#include <winnetwk.h>

#pragma comment(lib, "mpr.lib")

#include <iostream>
#include <string>
#include <stdexcept>

namespace drive_mapper {

// ---------------------------------------------------------------------------
// Helpers: widen / narrow strings
// ---------------------------------------------------------------------------
static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                  static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()), w.data(), len);
    return w;
}

static std::string fromWide(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                  static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(),
                        static_cast<int>(w.size()),
                        s.data(), len, nullptr, nullptr);
    return s;
}

static std::string winErrorString(DWORD code) {
    LPWSTR buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                   FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string msg;
    if (buf) {
        msg = fromWide(buf);
        LocalFree(buf);
        // trim trailing newlines
        while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n'))
            msg.pop_back();
    } else {
        msg = "Error " + std::to_string(code);
    }
    return msg;
}

// ---------------------------------------------------------------------------
// mapDrive – non-persistent via WNetAddConnection2
// ---------------------------------------------------------------------------
MapResult mapDrive(const ShareEntry& share) {
    MapResult result;
    result.driveLetter = share.driveLetter;
    result.success     = false;

    std::wstring wDrive = toWide(share.driveLetter);
    std::wstring wUnc   = toWide(share.uncPath);

    NETRESOURCEW nr = {};
    nr.dwType        = RESOURCETYPE_DISK;
    nr.lpLocalName   = wDrive.empty() ? nullptr : wDrive.data();
    nr.lpRemoteName  = wUnc.data();
    nr.lpProvider    = nullptr;

    LPCWSTR pUser = nullptr;
    LPCWSTR pPass = nullptr;
    std::wstring wUser, wPass;

    if (!share.username.empty()) {
        wUser = toWide(share.username);
        pUser = wUser.c_str();
    }
    if (!share.password.empty()) {
        wPass = toWide(share.password);
        pPass = wPass.c_str();
    }

    // CONNECT_TEMPORARY = not persistent (does not survive reboot/logoff)
    DWORD ret = WNetAddConnection2W(&nr, pPass, pUser, CONNECT_TEMPORARY);

    if (ret == NO_ERROR) {
        result.success = true;
    } else if (ret == ERROR_ALREADY_ASSIGNED || ret == ERROR_DEVICE_ALREADY_REMEMBERED) {
        // Drive letter already in use — unmap and retry once
        WNetCancelConnection2W(wDrive.data(), 0, TRUE);
        ret = WNetAddConnection2W(&nr, pPass, pUser, CONNECT_TEMPORARY);
        if (ret == NO_ERROR)
            result.success = true;
        else
            result.errorMessage = winErrorString(ret);
    } else {
        result.errorMessage = winErrorString(ret);
    }

    return result;
}

// ---------------------------------------------------------------------------
// unmapDrive
// ---------------------------------------------------------------------------
void unmapDrive(const std::string& driveLetter, bool force) {
    std::wstring wDrive = toWide(driveLetter);
    WNetCancelConnection2W(wDrive.data(), 0, force ? TRUE : FALSE);
}

// ---------------------------------------------------------------------------
// mapAll – map all shares, print results
// ---------------------------------------------------------------------------
int mapAll(const AppConfig& config) {
    int ok = 0;
    for (const auto& share : config.shares) {
        std::cout << "  Verbinde " << share.driveLetter
                  << " -> " << share.uncPath << " ... ";
        auto r = mapDrive(share);
        if (r.success) {
            std::cout << "OK\n";
            ++ok;
        } else {
            std::cout << "FEHLER: " << r.errorMessage << "\n";
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// unmapAll
// ---------------------------------------------------------------------------
void unmapAll(const AppConfig& config) {
    for (const auto& share : config.shares) {
        unmapDrive(share.driveLetter, true);
    }
}

} // namespace drive_mapper
