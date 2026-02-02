cmake_minimum_required(VERSION 3.16)
project(NvidiaAppLinux VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Generate version header from template
configure_file(
    ${CMAKE_SOURCE_DIR}/src/core/Version.h.in
    ${CMAKE_BINARY_DIR}/generated/Version.h
    @ONLY
)

set(SOURCES
    src/main.cpp
    src/core/Game.cpp
    src/core/DLSSSettings.cpp
    src/core/SettingsManager.cpp
    src/parsers/VDFParser.cpp
    src/launchers/LauncherManager.cpp
    src/launchers/SteamLauncher.cpp
    src/utils/EnvBuilder.cpp
    src/utils/ProtonManager.cpp
    src/network/ImageCache.cpp
    src/runner/GameRunner.cpp
    src/ui/MainWindow.cpp
    src/ui/GameListWidget.cpp
    src/ui/DLSSSettingsWidget.cpp
    src/ui/ProtonVersionDialog.cpp
)

set(HEADERS
    src/core/Game.h
    src/core/DLSSSettings.h
    src/core/SettingsManager.h
    src/parsers/VDFParser.h
    src/launchers/ILauncher.h
    src/launchers/LauncherManager.h
    src/launchers/SteamLauncher.h
    src/utils/EnvBuilder.h
    src/utils/ProtonManager.h
    src/network/ImageCache.h
    src/runner/GameRunner.h
    src/ui/MainWindow.h
    src/ui/GameListWidget.h
    src/ui/DLSSSettingsWidget.h
    src/ui/ProtonVersionDialog.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
