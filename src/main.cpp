#include <windows.h>

#include "config_manager.h"
#include "credential_store.h"
#include "drive_mapper.h"
#include "crypto.h"

#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Default config file path: same directory as the EXE
// ---------------------------------------------------------------------------
static std::string exeDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.parent_path().string();
}

static const std::string CONFIG_FILE = exeDir() + "\\shares.cfg";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void printBanner() {
    std::cout << "======================================\n";
    std::cout << "  Shareholder – NAS Drive Mapper\n";
    std::cout << "======================================\n\n";
}

static void printUsage(const char* exe) {
    std::cout << "Verwendung:\n"
              << "  " << exe << "            -- Laufwerke verbinden\n"
              << "  " << exe << " --setup    -- Neue Konfiguration erstellen\n"
              << "  " << exe << " --forget   -- Gespeichertes Passwort loeschen\n"
              << "  " << exe << " --unmap    -- Alle Laufwerke trennen\n"
              << "  " << exe << " --help     -- Diese Hilfe\n\n";
}

// ---------------------------------------------------------------------------
// Ask for password, verify it by decrypting config, return it on success.
// Retries up to 3 times.
// ---------------------------------------------------------------------------
static std::string getVerifiedPassword(const std::string& configPath,
                                       bool allowSaved) {
    // 1. Try saved credential first
    if (allowSaved) {
        auto saved = credential_store::loadPassword();
        if (saved.has_value()) {
            std::cout << "Gespeichertes Passwort wird verwendet...\n";
            try {
                config_manager::load(configPath, *saved);
                return *saved;
            } catch (...) {
                std::cout << "  Gespeichertes Passwort ungueltig. Bitte neu eingeben.\n";
                credential_store::deletePassword();
            }
        }
    }

    // 2. Prompt up to 3 times
    for (int attempt = 1; attempt <= 3; ++attempt) {
        std::string pw = credential_store::promptPassword("Passwort: ");
        try {
            config_manager::load(configPath, pw);
            // Ask whether to save
            std::cout << "Passwort im Windows Credential Manager speichern? [j/N]: ";
            std::string ans;
            std::getline(std::cin, ans);
            if (!ans.empty() && (ans[0] == 'j' || ans[0] == 'J' ||
                                 ans[0] == 'y' || ans[0] == 'Y')) {
                credential_store::storePassword(pw);
                std::cout << "  Passwort gespeichert.\n";
            }
            return pw;
        } catch (const std::exception& ex) {
            std::cerr << "  Fehler: " << ex.what() << "\n";
            if (attempt < 3)
                std::cout << "  Bitte erneut versuchen (" << attempt << "/3)...\n";
        }
    }
    throw std::runtime_error("Zu viele Fehlversuche. Programm wird beendet.");
}

// ---------------------------------------------------------------------------
// --setup flow
// ---------------------------------------------------------------------------
static void runSetup() {
    std::cout << "=== Einrichtung ===\n\n";

    // Get (new) password
    std::string pw1 = credential_store::promptPassword("Neues Passwort: ");
    std::string pw2 = credential_store::promptPassword("Passwort bestaetigen: ");
    if (pw1 != pw2)
        throw std::runtime_error("Passwoerter stimmen nicht ueberein.");
    if (pw1.empty())
        throw std::runtime_error("Passwort darf nicht leer sein.");

    // Build config interactively
    AppConfig cfg = config_manager::createInteractive();
    if (cfg.shares.empty())
        throw std::runtime_error("Keine Shares konfiguriert.");

    // Save encrypted
    config_manager::save(CONFIG_FILE, pw1, cfg);
    std::cout << "\nKonfiguration gespeichert: " << CONFIG_FILE << "\n";

    // Offer to save password
    std::cout << "Passwort im Windows Credential Manager speichern? [j/N]: ";
    std::string ans;
    std::getline(std::cin, ans);
    if (!ans.empty() && (ans[0] == 'j' || ans[0] == 'J' ||
                         ans[0] == 'y' || ans[0] == 'Y')) {
        credential_store::storePassword(pw1);
        std::cout << "Passwort gespeichert.\n";
    }

    std::cout << "\nEinrichtung abgeschlossen. Starten Sie das Programm ohne "
                 "Argumente, um die Laufwerke zu verbinden.\n";
}

// ---------------------------------------------------------------------------
// Main connect flow
// ---------------------------------------------------------------------------
static void runConnect() {
    if (!config_manager::exists(CONFIG_FILE)) {
        std::cout << "Keine Konfigurationsdatei gefunden.\n\n";
        std::cout << "Starten Sie das Programm mit --setup, um die "
                     "Konfiguration zu erstellen.\n";
        return;
    }

    std::string pw = getVerifiedPassword(CONFIG_FILE, true);
    AppConfig cfg  = config_manager::load(CONFIG_FILE, pw);

    std::cout << "\nVerbinde " << cfg.shares.size() << " Laufwerk(e)...\n";
    int ok = drive_mapper::mapAll(cfg);
    std::cout << "\n" << ok << "/" << cfg.shares.size()
              << " Laufwerke erfolgreich verbunden.\n";
}

// ---------------------------------------------------------------------------
// --unmap flow
// ---------------------------------------------------------------------------
static void runUnmap() {
    if (!config_manager::exists(CONFIG_FILE)) {
        std::cerr << "Keine Konfigurationsdatei gefunden.\n";
        return;
    }
    std::string pw = getVerifiedPassword(CONFIG_FILE, true);
    AppConfig cfg  = config_manager::load(CONFIG_FILE, pw);

    std::cout << "Trenne Laufwerke...\n";
    drive_mapper::unmapAll(cfg);
    std::cout << "Erledigt.\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Force UTF-8 console output on Windows
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    printBanner();

    try {
        if (argc >= 2) {
            std::string arg = argv[1];
            if (arg == "--help" || arg == "-h") {
                printUsage(argv[0]);
                return 0;
            }
            if (arg == "--setup") {
                runSetup();
                return 0;
            }
            if (arg == "--forget") {
                credential_store::deletePassword();
                std::cout << "Gespeichertes Passwort wurde geloescht.\n";
                return 0;
            }
            if (arg == "--unmap") {
                runUnmap();
                return 0;
            }
            std::cerr << "Unbekanntes Argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }

        // Default: connect drives
        runConnect();

    } catch (const std::exception& ex) {
        std::cerr << "\nFEHLER: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
