#include <windows.h>

#include "config_manager.h"
#include "credential_store.h"
#include "drive_mapper.h"
#include "crypto.h"

#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
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
    std::cout <<
        "Verwendung: " << exe << " [BEFEHL] [OPTIONEN]\n"
        "\n"
        "BEFEHLE (Konfiguration):\n"
        "  (kein Argument)              Alle Laufwerke verbinden\n"
        "  --setup                      Neue Konfiguration interaktiv erstellen\n"
        "  --list                       Alle konfigurierten Shares anzeigen\n"
        "  --add                        Einen neuen Share interaktiv hinzufuegen\n"
        "  --remove <Laufwerk>          Share entfernen, z.B. --remove Z:\n"
        "  --show <Laufwerk>            Details eines Shares anzeigen\n"
        "\n"
        "BEFEHLE (Laufwerke):\n"
        "  --unmap                      Alle Laufwerke trennen\n"
        "  --unmap <Laufwerk>           Einzelnes Laufwerk trennen, z.B. --unmap Z:\n"
        "\n"
        "BEFEHLE (Passwort):\n"
        "  --forget                     Gespeichertes Passwort aus Credential Manager loeschen\n"
        "\n"
        "SONSTIGES:\n"
        "  --help, -h                   Diese Hilfe anzeigen\n"
        "\n"
        "BEISPIELE:\n"
        "  " << exe << " --setup         Ersteinrichtung\n"
        "  " << exe << "                 Alle Laufwerke verbinden\n"
        "  " << exe << " --list          Konfigurierte Shares anzeigen\n"
        "  " << exe << " --add           Neuen Share hinzufuegen\n"
        "  " << exe << " --remove Z:     Share Z: loeschen\n"
        "  " << exe << " --unmap Z:      Nur Laufwerk Z: trennen\n"
        "\n";
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
// --unmap [Laufwerk]
// ---------------------------------------------------------------------------
static void runUnmap(const std::string& drive = "") {
    if (!config_manager::exists(CONFIG_FILE)) {
        std::cerr << "Keine Konfigurationsdatei gefunden.\n";
        return;
    }
    std::string pw = getVerifiedPassword(CONFIG_FILE, true);
    AppConfig cfg  = config_manager::load(CONFIG_FILE, pw);

    if (drive.empty()) {
        std::cout << "Trenne alle Laufwerke...\n";
        drive_mapper::unmapAll(cfg);
        std::cout << "Erledigt.\n";
    } else {
        drive_mapper::unmapDrive(drive, true);
        std::cout << "Laufwerk " << drive << " getrennt.\n";
    }
}

// ---------------------------------------------------------------------------
// --list  – alle konfigurierten Shares anzeigen
// ---------------------------------------------------------------------------
static void runList() {
    if (!config_manager::exists(CONFIG_FILE)) {
        std::cout << "Keine Konfigurationsdatei gefunden. Bitte --setup ausfuehren.\n";
        return;
    }
    std::string pw = getVerifiedPassword(CONFIG_FILE, true);
    AppConfig cfg  = config_manager::load(CONFIG_FILE, pw);

    if (cfg.shares.empty()) {
        std::cout << "Keine Shares konfiguriert.\n";
        return;
    }
    std::cout << "Konfigurierte Shares (" << cfg.shares.size() << "):\n\n";
    std::cout << "  Nr  Laufwerk  UNC-Pfad                          Benutzer\n";
    std::cout << "  --- --------- --------------------------------- --------------------\n";
    for (size_t i = 0; i < cfg.shares.size(); ++i) {
        const auto& s = cfg.shares[i];
        std::cout << "  [" << (i + 1) << "]  "
                  << s.driveLetter
                  << "        "
                  << s.uncPath;
        // pad to column
        if (s.uncPath.size() < 33)
            std::cout << std::string(33 - s.uncPath.size(), ' ');
        std::cout << "  " << (s.username.empty() ? "(aktueller Benutzer)" : s.username)
                  << "\n";
    }
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// --show <Laufwerk>
// ---------------------------------------------------------------------------
static void runShow(const std::string& drive) {
    if (!config_manager::exists(CONFIG_FILE)) {
        std::cerr << "Keine Konfigurationsdatei gefunden.\n";
        return;
    }
    std::string pw = getVerifiedPassword(CONFIG_FILE, true);
    AppConfig cfg  = config_manager::load(CONFIG_FILE, pw);

    for (const auto& s : cfg.shares) {
        if (s.driveLetter == drive) {
            std::cout << "\nShare-Details: " << drive << "\n";
            std::cout << "  Laufwerk : " << s.driveLetter << "\n";
            std::cout << "  UNC-Pfad : " << s.uncPath     << "\n";
            std::cout << "  Benutzer : " << (s.username.empty() ? "(aktueller Benutzer)" : s.username) << "\n";
            std::cout << "  Passwort : " << (s.password.empty() ? "(keines)" : "***") << "\n\n";
            return;
        }
    }
    std::cerr << "Kein Share mit Laufwerk '" << drive << "' gefunden.\n";
}

// ---------------------------------------------------------------------------
// --add  – einzelnen Share interaktiv hinzufuegen
// ---------------------------------------------------------------------------
static void runAdd() {
    if (!config_manager::exists(CONFIG_FILE)) {
        std::cerr << "Keine Konfigurationsdatei gefunden. Bitte zuerst --setup ausfuehren.\n";
        return;
    }
    std::string pw = getVerifiedPassword(CONFIG_FILE, true);
    AppConfig cfg  = config_manager::load(CONFIG_FILE, pw);

    std::cout << "\n=== Neuen Share hinzufuegen ===\n\n";

    ShareEntry entry;
    std::cout << "Laufwerksbuchstabe (z.B. Z:): ";
    std::getline(std::cin, entry.driveLetter);
    if (entry.driveLetter.empty())
        throw std::runtime_error("Kein Laufwerksbuchstabe angegeben.");
    if (entry.driveLetter.size() == 1)
        entry.driveLetter += ':';

    // Check for duplicate
    for (const auto& s : cfg.shares) {
        if (s.driveLetter == entry.driveLetter)
            throw std::runtime_error("Laufwerk '" + entry.driveLetter + "' ist bereits konfiguriert. Bitte zuerst --remove ausfuehren.");
    }

    std::cout << "UNC-Pfad (z.B. \\\\nas\\share): ";
    std::getline(std::cin, entry.uncPath);
    if (entry.uncPath.empty())
        throw std::runtime_error("Kein UNC-Pfad angegeben.");

    std::cout << "Benutzername (leer = aktueller Windows-Benutzer): ";
    std::getline(std::cin, entry.username);

    if (!entry.username.empty()) {
        std::cout << "Share-Passwort: ";
        std::getline(std::cin, entry.password);
    }

    cfg.shares.push_back(std::move(entry));
    config_manager::save(CONFIG_FILE, pw, cfg);
    std::cout << "\nShare hinzugefuegt und Konfiguration gespeichert.\n";
}

// ---------------------------------------------------------------------------
// --remove <Laufwerk>
// ---------------------------------------------------------------------------
static void runRemove(const std::string& drive) {
    if (!config_manager::exists(CONFIG_FILE)) {
        std::cerr << "Keine Konfigurationsdatei gefunden.\n";
        return;
    }
    std::string pw = getVerifiedPassword(CONFIG_FILE, true);
    AppConfig cfg  = config_manager::load(CONFIG_FILE, pw);

    auto it = std::find_if(cfg.shares.begin(), cfg.shares.end(),
                           [&](const ShareEntry& s) { return s.driveLetter == drive; });
    if (it == cfg.shares.end()) {
        std::cerr << "Kein Share mit Laufwerk '" << drive << "' gefunden.\n";
        return;
    }

    std::cout << "Share loeschen: " << drive << " -> " << it->uncPath << "\n";
    std::cout << "Bestaetigen? [j/N]: ";
    std::string ans;
    std::getline(std::cin, ans);
    if (ans.empty() || (ans[0] != 'j' && ans[0] != 'J' && ans[0] != 'y' && ans[0] != 'Y')) {
        std::cout << "Abgebrochen.\n";
        return;
    }

    cfg.shares.erase(it);
    config_manager::save(CONFIG_FILE, pw, cfg);
    std::cout << "Share '" << drive << "' geloescht und Konfiguration gespeichert.\n";
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
                std::string drive = (argc >= 3) ? argv[2] : "";
                runUnmap(drive);
                return 0;
            }
            if (arg == "--list") {
                runList();
                return 0;
            }
            if (arg == "--show") {
                if (argc < 3) throw std::runtime_error("--show benoetigt einen Laufwerksbuchstaben, z.B.: --show Z:");
                runShow(argv[2]);
                return 0;
            }
            if (arg == "--add") {
                runAdd();
                return 0;
            }
            if (arg == "--remove") {
                if (argc < 3) throw std::runtime_error("--remove benoetigt einen Laufwerksbuchstaben, z.B.: --remove Z:");
                runRemove(argv[2]);
                return 0;
            }
            std::cerr << "Unbekanntes Argument: " << arg << "\n\n";
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
