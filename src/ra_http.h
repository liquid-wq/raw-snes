// HTTPS zu RetroAchievements ueber WinHTTP (gehoert zu Windows).
//
// Parameterkodierung wie quote_plus, Zeitgrenze 12 s, HTTP 429 (Drosselung)
// wird als RateLimited gemeldet.
#pragma once

#include "ra_client.h"

#include <map>
#include <string>

constexpr const char* RA_HOST = "retroachievements.org";
constexpr const char* RA_USER_AGENT = "RAW-SNES/1.0";

std::string ra_urlencode(const std::map<std::string, std::string>& params);

// Synchroner RequestFn fuer RaClient. Wirft RateLimited bei HTTP 429 und
// std::runtime_error bei Netz- oder JSON-Fehlern.
RequestFn make_winhttp_request_fn();

// Einfacher GET, z. B. fuer Badges. true und `body` bei HTTP 2xx.
bool http_get(const std::string& host, const std::string& path, int timeoutMs,
              const char* userAgent, std::string* body);
