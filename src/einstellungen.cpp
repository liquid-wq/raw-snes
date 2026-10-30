#include "einstellungen.h"

#include "json.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string Text(const Json& o, const char* schluessel, const std::string& standard = std::string()) {
    const Json* v = o.find(schluessel);
    return (v && v->type() == Json::Type::String) ? v->as_string() : standard;
}

bool Schalter(const Json& o, const char* schluessel, bool standard) {
    const Json* v = o.find(schluessel);
    return (v && v->type() == Json::Type::Bool) ? v->as_bool() : standard;
}

}  // namespace

void Einstellungen::Laden(const std::string& pfad) {
    std::ifstream f(std::filesystem::u8path(pfad), std::ios::binary);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    Json obj;
    try {
        obj = Json::parse(ss.str());
    } catch (...) {
        return;
    }
    if (obj.type() != Json::Type::Object) return;

    raUser = Text(obj, "ra_user");
    raToken = Text(obj, "ra_token");   // ein altes "ra_pass" wird nicht mehr gelesen
    rememberLogin = Schalter(obj, "remember_login", true);
    language = Text(obj, "language", "de");
    autostart = Schalter(obj, "autostart", true);
    hardcore = Schalter(obj, "hardcore", false);

    hashes.clear();
    if (const Json* h = obj.find("hashes")) {
        if (h->type() == Json::Type::Object) {
            for (const auto& kv : h->entries())
                if (kv.second.type() == Json::Type::String) hashes[kv.first] = kv.second.as_string();
        }
    }
}

void Einstellungen::Speichern(const std::string& pfad) const {
    Json obj = Json::object();
    // Ohne "Eingeloggt bleiben" werden Benutzername und Schluessel NICHT
    // abgelegt; der Haken selbst wird immer gespeichert. Das Passwort wird nie
    // abgelegt, nur der Schluessel, den RetroAchievements beim Anmelden ausgibt.
    obj.set("remember_login", Json::boolean(rememberLogin));
    obj.set("ra_user", Json::string(rememberLogin ? raUser : std::string()));
    obj.set("ra_token", Json::string(rememberLogin ? raToken : std::string()));
    obj.set("language", Json::string(language));
    obj.set("autostart", Json::boolean(autostart));
    obj.set("hardcore", Json::boolean(hardcore));
    Json h = Json::object();
    for (const auto& kv : hashes) h.set(kv.first, Json::string(kv.second));
    obj.set("hashes", h);

    std::ofstream f(std::filesystem::u8path(pfad), std::ios::binary | std::ios::trunc);
    if (f) f << obj.dump();
}
