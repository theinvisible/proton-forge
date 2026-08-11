cmake_minimum_required(VERSION 3.16)
project(ProtonForge VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent DBus)

# zlib: GOG's content system serves every manifest and every chunk zlib-encoded
# with no Content-Encoding header, so Qt will not inflate them and qUncompress()
# cannot (it expects Qt's own 4-byte length prefix).
find_package(ZLIB REQUIRED)

# QtKeychain is optional. Where it is missing — or where no secret service is
# running, which is the normal state on a bare gaming session — SecretStore
# falls back to a 0600 file. One code path is always compiled; see SecretStore.h.
find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND)
    message(STATUS "QtKeychain found — credentials will use the system keyring")
else()
    message(STATUS "QtKeychain not found — credentials fall back to a 0600 file")
endif()

# Off by default: the Flatpak build and packaging never need it, and enabling it
# pulls in Qt6::Test. CI turns it on for the debug build.
option(PROTONFORGE_BUILD_TESTS "Build the unit tests (tests/unit)" OFF)

# Generate version header from template
configure_file(
    ${CMAKE_SOURCE_DIR}/src/core/Version.h.in
    ${CMAKE_BINARY_DIR}/generated/Version.h
    @ONLY
)

# Everything below the widgets. Split out of the executable so the unit tests
# can link the real code instead of re-listing its sources — src/ui is what
# needs a screen, and nothing here does.
set(CORE_SOURCES
    src/core/Cli.cpp
    src/core/Game.cpp
    src/core/SecretStore.cpp
    src/core/DLSSSettings.cpp
    src/core/FeatureGate.cpp
    src/core/SettingsManager.cpp
    src/parsers/VDFParser.cpp
    src/launchers/LauncherManager.cpp
    src/launchers/SteamLauncher.cpp
    src/launchers/GogLauncher.cpp
    src/utils/EnvBuilder.cpp
    src/utils/ProcessRunner.cpp
    src/utils/ProtonManager.cpp
    src/utils/LaunchOptionExtractor.cpp
    src/utils/GpuInfoCache.cpp
    src/utils/SteamPaths.cpp
    src/utils/SteamClient.cpp
    src/utils/CPUDetector.cpp
    src/utils/HDRChecker.cpp
    src/utils/GPUDetector.cpp
    src/utils/NvidiaGPUDetector.cpp
    src/utils/NvmlSession.cpp
    src/utils/DisplayDetector.cpp
    src/utils/KScreenDoctor.cpp
    src/utils/KdeDisplayProbe.cpp
    src/gog/GogAuth.cpp
    src/gog/GogRequest.cpp
    src/gog/GogApiClient.cpp
    src/gog/GogStoreService.cpp
    src/gog/GogContentClient.cpp
    src/gog/GogInstallPlan.cpp
    src/gog/GogInstallRegistry.cpp
    src/gog/GogPlayTasks.cpp
    src/gog/GogDownloader.cpp
    src/gog/ZipReader.cpp
    src/gog/GogOfflineClient.cpp
    src/launchers/SteamStoreService.cpp
    src/network/JsonDiskCache.cpp
    src/network/ImageCache.cpp
    src/network/ProtonDBClient.cpp
    src/runner/GameRunner.cpp
)

set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/GameListWidget.cpp
    src/ui/DLSSSettingsWidget.cpp
    src/ui/ProtonVersionDialog.cpp
    src/ui/SystemInfoDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/AboutDialog.cpp
    src/ui/MangoHudDialog.cpp
    src/ui/RecommendationsDialog.cpp
    src/ui/GogLoginDialog.cpp
    src/ui/StoreLibraryDialog.cpp
)

set(CORE_HEADERS
    src/core/Cli.h
    src/core/Game.h
    src/core/LauncherTraits.h
    src/core/SecretStore.h
    src/core/DLSSSettings.h
    src/core/FeatureGate.h
    src/core/SettingsManager.h
    src/parsers/VDFParser.h
    src/launchers/ILauncher.h
    src/launchers/LauncherManager.h
    src/launchers/SteamLauncher.h
    src/launchers/GogLauncher.h
    src/utils/EnvBuilder.h
    src/utils/ProcessRunner.h
    src/utils/ProtonManager.h
    src/utils/LaunchOptionExtractor.h
    src/utils/GpuInfoCache.h
    src/utils/SteamPaths.h
    src/utils/SteamClient.h
    src/utils/CPUDetector.h
    src/utils/HDRChecker.h
    src/utils/GPUDetector.h
    src/utils/NvidiaGPUDetector.h
    src/utils/NvmlSession.h
    src/utils/IGpuTelemetrySource.h
    src/utils/DisplayInfo.h
    src/utils/IDisplayProbe.h
    src/utils/KScreenDoctor.h
    src/utils/KdeDisplayProbe.h
    src/utils/DisplayDetector.h
    src/gog/GogAuth.h
    src/gog/GogRequest.h
    src/gog/GogApiClient.h
    src/gog/GogStoreService.h
    src/gog/GogContentClient.h
    src/gog/GogInstallPlan.h
    src/gog/GogInstallRegistry.h
    src/gog/GogPlayTasks.h
    src/gog/GogDownloader.h
    src/gog/ZipReader.h
    src/gog/GogOfflineClient.h
    src/launchers/IStoreService.h
    src/launchers/SteamStoreService.h
    src/network/JsonDiskCache.h
    src/network/ImageCache.h
    src/network/ProtonDBClient.h
    src/runner/GameRunner.h
)

set(UI_HEADERS
    src/ui/MainWindow.h
    src/ui/GameListWidget.h
    src/ui/DLSSSettingsWidget.h
    src/ui/ProtonVersionDialog.h
    src/ui/SystemInfoDialog.h
    src/ui/SettingsDialog.h
    src/ui/AboutDialog.h
    src/ui/AppStyle.h
    src/ui/MangoHudDialog.h
    src/ui/RecommendationsDialog.h
    src/ui/GogLoginDialog.h
    src/ui/StoreLibraryDialog.h
    src/ui/BadgeRow.h
    src/ui/StoreVisuals.h
    src/ui/MangoHudPreview.h
)

set(RESOURCES
    resources.qrc
)

add_library(protonforge_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(protonforge_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

# Widgets stays here rather than on the executable: a handful of non-UI classes
# still reach for it, and splitting that out would be a change to the code
# rather than to the build.
target_link_libraries(protonforge_core PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::DBus
    ZLIB::ZLIB
    ${CMAKE_DL_LIBS}   # dlopen() for NVML (libnvidia-ml) — loaded at runtime, not linked
)

if(Qt6Keychain_FOUND)
    target_link_libraries(protonforge_core PUBLIC Qt6Keychain::Qt6Keychain)
    target_compile_definitions(protonforge_core PUBLIC PROTONFORGE_HAVE_QTKEYCHAIN)
endif()

add_executable(${PROJECT_NAME} src/main.cpp ${UI_SOURCES} ${UI_HEADERS} ${RESOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE protonforge_core)

if(PROTONFORGE_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Install icon
install(FILES org.protonforge.ProtonForge.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME protonforge.svg
)

# Install desktop file
configure_file(
    ${CMAKE_SOURCE_DIR}/protonforge.desktop.in
    ${CMAKE_BINARY_DIR}/protonforge.desktop
    @ONLY
)
install(FILES ${CMAKE_BINARY_DIR}/protonforge.desktop
    DESTINATION share/applications
)
