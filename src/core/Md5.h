// Md5.h -- Eigenstaendige MD5-Implementierung nach dem oeffentlichen
// RFC-1321-Algorithmus. Wird gebraucht fuer snes_hash() (ROM-Identifikation
// gegen die RA-Datenbank) und die Verifizierungshashes bei RA-API-Aufrufen
// (awardachievement, submitlbentry).
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace rawsnes {

// Berechnet den MD5-Hash der uebergebenen Bytes, gibt ihn als 32-stelligen
// Hex-String (Kleinbuchstaben) zurueck.
std::string md5Hex(const uint8_t* data, size_t len);
std::string md5Hex(const std::string& s);
std::string md5Hex(const std::vector<uint8_t>& data);

} // namespace rawsnes
