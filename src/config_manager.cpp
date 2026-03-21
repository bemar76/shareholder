#include "config_manager.h"
#include "crypto.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>

// ---------------------------------------------------------------------------
// Minimal JSON serialiser/deserialiser (no external dependency)
// Format:
// {
//   "shares": [
//     { "drive": "Z:", "unc": "\\\\nas\\share", "user": "", "password": "" },
//     ...
//   ]
// }
// ---------------------------------------------------------------------------

namespace {

// --- tiny JSON builder -------------------------------------------------------

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static std::string configToJson(const AppConfig& cfg) {
    std::ostringstream o;
    o << "{\n  \"shares\": [\n";
    for (size_t i = 0; i < cfg.shares.size(); ++i) {
        const auto& s = cfg.shares[i];
        o << "    {\n";
        o << "      \"drive\": \""    << jsonEscape(s.driveLetter) << "\",\n";
        o << "      \"unc\": \""      << jsonEscape(s.uncPath)     << "\",\n";
        o << "      \"user\": \""     << jsonEscape(s.username)    << "\",\n";
        o << "      \"password\": \"" << jsonEscape(s.password)    << "\"\n";
        o << "    }";
        if (i + 1 < cfg.shares.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

// --- tiny JSON parser --------------------------------------------------------
// Parses only the specific schema produced by configToJson().

static void skipWhitespace(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' ||
                               s[pos] == '\r' || s[pos] == '\t'))
        ++pos;
}

static std::string parseString(const std::string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '"')
        throw std::runtime_error("JSON parse error: expected '\"' at " + std::to_string(pos));
    ++pos;
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\') {
            ++pos;
            if (pos >= s.size()) break;
            switch (s[pos]) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += s[pos]; break;
            }
        } else {
            result += s[pos];
        }
        ++pos;
    }
    if (pos < s.size()) ++pos; // consume closing '"'
    return result;
}

static void expect(const std::string& s, size_t& pos, char c) {
    skipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != c)
        throw std::runtime_error(std::string("JSON parse error: expected '") +
                                 c + "' at " + std::to_string(pos));
    ++pos;
}

static std::string parseKey(const std::string& s, size_t& pos) {
    skipWhitespace(s, pos);
    auto key = parseString(s, pos);
    skipWhitespace(s, pos);
    expect(s, pos, ':');
    return key;
}

static AppConfig jsonToConfig(const std::string& json) {
    AppConfig cfg;
    size_t pos = 0;
    expect(json, pos, '{');
    // read "shares" key
    auto k = parseKey(json, pos);
    if (k != "shares")
        throw std::runtime_error("JSON: expected key 'shares'");
    skipWhitespace(json, pos);
    expect(json, pos, '[');
    skipWhitespace(json, pos);
    while (pos < json.size() && json[pos] != ']') {
        expect(json, pos, '{');
        ShareEntry entry;
        for (int field = 0; field < 4; ++field) {
            auto fk = parseKey(json, pos);
            skipWhitespace(json, pos);
            auto fv = parseString(json, pos);
            if      (fk == "drive")    entry.driveLetter = fv;
            else if (fk == "unc")      entry.uncPath     = fv;
            else if (fk == "user")     entry.username    = fv;
            else if (fk == "password") entry.password    = fv;
            skipWhitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') ++pos;
        }
        expect(json, pos, '}');
        cfg.shares.push_back(std::move(entry));
        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') ++pos;
        skipWhitespace(json, pos);
    }
    return cfg;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace config_manager {

bool exists(const std::string& filePath) {
    return std::filesystem::exists(filePath);
}

AppConfig load(const std::string& filePath, const std::string& password) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open config file: " + filePath);

    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    auto plain = crypto::decrypt(password, blob);
    std::string json(plain.begin(), plain.end());
    return jsonToConfig(json);
}

void save(const std::string& filePath, const std::string& password,
          const AppConfig& config) {
    std::string json = configToJson(config);
    std::vector<uint8_t> plain(json.begin(), json.end());
    auto blob = crypto::encrypt(password, plain);

    std::ofstream f(filePath, std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("Cannot write config file: " + filePath);
    f.write(reinterpret_cast<const char*>(blob.data()), blob.size());
}

AppConfig createInteractive() {
    AppConfig cfg;
    std::cout << "\n=== Neue Konfiguration erstellen ===\n";
    std::cout << "Geben Sie die NAS-Shares ein (leer lassen zum Beenden):\n\n";

    while (true) {
        ShareEntry entry;
        std::cout << "Laufwerksbuchstabe (z.B. Z:, leer = fertig): ";
        std::getline(std::cin, entry.driveLetter);
        if (entry.driveLetter.empty()) break;

        // Normalise: ensure it ends with ':'
        if (entry.driveLetter.size() == 1)
            entry.driveLetter += ':';

        std::cout << "UNC-Pfad (z.B. \\\\nas\\share): ";
        std::getline(std::cin, entry.uncPath);

        std::cout << "Benutzername (leer = aktueller Windows-Benutzer): ";
        std::getline(std::cin, entry.username);

        if (!entry.username.empty()) {
            std::cout << "Share-Passwort: ";
            std::getline(std::cin, entry.password);
        }

        cfg.shares.push_back(std::move(entry));
        std::cout << "  --> Share hinzugefuegt.\n\n";
    }
    return cfg;
}

} // namespace config_manager
