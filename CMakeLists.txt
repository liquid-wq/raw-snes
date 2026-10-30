cmake_minimum_required(VERSION 3.20)
project(rawsnes CXX C RC)

# ---------------------------------------------------------------------------
# Ziel: EINE Datei. Keine Begleit-DLLs ausser denen, die zu Windows gehoeren.
#
# Aufbau wie bei RAW-NES: Dear ImGui auf Win32 + OpenGL2, MinGW-Laufzeit
# statisch gebunden. Intro-Bilder und Programmsymbol stecken als
# Windows-Ressourcen in der Exe (siehe src/app.rc.in).
#
# src/core enthaelt nur Code ohne Oberflaechen-Bezug; Protokoll, Speicherkarte
# und Header-Erkennung sind zusaetzlich ohne Betriebssystem-Bezug.
#
# Fremdquellen liegen ABSICHTLICH ausserhalb des Projektordners.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version und Build-Nummer. EINZIGE Quelle. Die Build-Nummer bei jeder
# Aenderung hochsetzen, die Version bei jeder Veroeffentlichung. Beide stehen
# im Fenster ("RAW-SNES 1.0.0 (Build N)"), die Build-Nummer zusaetzlich im
# Dateinamen (rawsnes_bN.exe).
set(VERSION_TEXT "1.0.0")
set(BUILD_NR 138)
string(REPLACE "." "," VERSION_KOMMA "${VERSION_TEXT}")

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/../imgui" CACHE PATH "Ordner mit imgui.cpp (Dear ImGui)")
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Fehlt: ${IMGUI_DIR}/imgui.cpp")
endif()

set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
configure_file(src/app.rc.in "${CMAKE_BINARY_DIR}/erzeugt/app.rc" @ONLY)

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    # OpenGL2 und nicht OpenGL3: Windows ohne Grafiktreiber liefert nur
    # "OpenGL 1.1.0 / GDI Generic", dort stuerzt die OpenGL3-Anbindung ab.
    "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp"
)

# Ohne Betriebssystem-Bezug.
set(PROTOKOLL_SOURCES
    src/core/fxpak_protokoll.cpp
    src/core/snes_kopf.cpp
    src/core/snes_speicher.cpp
)

# Kernschicht: ohne jeden Oberflaechen-Bezug.
set(CORE_SOURCES
    ${PROTOKOLL_SOURCES}
    src/core/Md5.cpp
    src/core/json.cpp
    src/core/ra_cache.cpp
    src/core/ra_client.cpp
    src/core/ConditionEngine.cpp
    src/core/nmi_kopie.cpp
)

set(AUSGABE_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Ablage der fertigen Programme")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX RAWSNES_BUILD=${BUILD_NR}
                        RAWSNES_VERSION="${VERSION_TEXT}")

add_executable(rawsnes WIN32
    src/main.cpp
    "${CMAKE_BINARY_DIR}/erzeugt/app.rc"
    src/app.cpp
    src/intro.cpp
    src/popup.cpp
    src/bilder.cpp
    src/i18n.cpp
    src/worker.cpp
    src/fxpak_port.cpp
    src/ra_http.cpp
    src/einstellungen.cpp
    src/plattform.cpp
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
)
target_include_directories(rawsnes PRIVATE src src/core "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_compile_definitions(rawsnes PRIVATE UNICODE _UNICODE)
target_link_libraries(rawsnes PRIVATE
    opengl32 gdi32 user32 shell32 comdlg32 dwmapi
    setupapi        # Auflisten der seriellen Anschluesse samt USB-Kennung
    winhttp         # HTTPS zu RetroAchievements
    gdiplus ole32   # PNG dekodieren, Freischalt-Einblendung
    winmm           # feinere Zeitscheiben fuer den Abfragetakt
)
set_target_properties(rawsnes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${AUSGABE_DIR}"
    # Build-Nummer im Dateinamen: ein neuer Build kollidiert nie mit einer
    # alten Exe, die noch laeuft oder gesperrt ist.
    OUTPUT_NAME "rawsnes_b${BUILD_NR}")

# Selbsttest: Kernschicht, Port und Worker gegen ein simuliertes FXPak und
# einen simulierten RA-Server. Kein Geraet, kein Netz, kein Fenster.
add_executable(selbsttest
    src/selbsttest.cpp
    src/test/fxpak_sim.cpp
    src/test/cpu65816.cpp
    src/worker.cpp
    src/fxpak_port.cpp
    src/rom_vergleich.cpp
    src/i18n.cpp
    ${CORE_SOURCES}
)
target_include_directories(selbsttest PRIVATE src src/core src/test)
target_link_libraries(selbsttest PRIVATE setupapi winmm)
set_target_properties(selbsttest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${AUSGABE_DIR}")

if(MINGW)
    # Keine libstdc++/libgcc/libwinpthread daneben. -municode, weil der
    # Einstiegspunkt wWinMain heisst. -s: Symbole entfernen.
    target_link_options(rawsnes PRIVATE -municode -static -static-libgcc -static-libstdc++ -s)
    target_link_options(selbsttest PRIVATE -static -static-libgcc -static-libstdc++)
endif()
