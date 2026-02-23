cmake_minimum_required(VERSION 3.20)
project(KT-WinDiagTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(NOT WIN32)
    message(FATAL_ERROR "KT-WinDiagTool is a Windows-only application")
endif()

add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)

# Fetch third-party dependencies
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    minizip
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json)

# Build zlib for minizip
FetchContent_GetProperties(minizip)
if(NOT minizip_POPULATED)
    FetchContent_Populate(minizip)
endif()

# zlib sources
set(ZLIB_SOURCES
    ${minizip_SOURCE_DIR}/adler32.c
    ${minizip_SOURCE_DIR}/compress.c
    ${minizip_SOURCE_DIR}/crc32.c
    ${minizip_SOURCE_DIR}/deflate.c
    ${minizip_SOURCE_DIR}/gzclose.c
    ${minizip_SOURCE_DIR}/gzlib.c
    ${minizip_SOURCE_DIR}/gzread.c
    ${minizip_SOURCE_DIR}/gzwrite.c
    ${minizip_SOURCE_DIR}/inflate.c
    ${minizip_SOURCE_DIR}/infback.c
    ${minizip_SOURCE_DIR}/inftrees.c
    ${minizip_SOURCE_DIR}/inffast.c
    ${minizip_SOURCE_DIR}/trees.c
    ${minizip_SOURCE_DIR}/uncompr.c
    ${minizip_SOURCE_DIR}/zutil.c
)

# minizip sources
set(MINIZIP_SOURCES
    ${minizip_SOURCE_DIR}/contrib/minizip/zip.c
    ${minizip_SOURCE_DIR}/contrib/minizip/ioapi.c
    ${minizip_SOURCE_DIR}/contrib/minizip/iowin32.c
)

add_library(minizip_lib STATIC ${ZLIB_SOURCES} ${MINIZIP_SOURCES})
target_include_directories(minizip_lib PUBLIC
    ${minizip_SOURCE_DIR}
    ${minizip_SOURCE_DIR}/contrib/minizip
)
target_compile_definitions(minizip_lib PRIVATE _CRT_SECURE_NO_WARNINGS)

# Application sources
set(APP_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/Config.cpp
    src/app/CLIHandler.cpp
    src/util/Logger.cpp
    src/util/StringUtils.cpp
    src/util/Privileges.cpp
    src/util/WmiHelper.cpp
)

# Collector sources
set(COLLECTOR_SOURCES
    src/collectors/CollectorEngine.cpp
    src/collectors/EnvVarCollector.cpp
    src/collectors/RegistryCollector.cpp
    src/collectors/InstalledProductsCollector.cpp
    src/collectors/ProductLogCollector.cpp
    src/collectors/InstallLogCollector.cpp
    src/collectors/ServicesCollector.cpp
    src/collectors/ProxySettingsCollector.cpp
    src/collectors/NetworkConfigCollector.cpp
    src/collectors/FirewallConfigCollector.cpp
    src/collectors/UserInfoCollector.cpp
    src/collectors/DiskSpaceCollector.cpp
    src/collectors/WerCollector.cpp
    src/collectors/SystemInfoCollector.cpp
    src/collectors/EventLogCollector.cpp
    src/collectors/EtwLogCollector.cpp
    src/collectors/WppLogCollector.cpp
    src/collectors/PerfLogCollector.cpp
    src/collectors/PacketCaptureCollector.cpp
    src/collectors/CrashDumpCollector.cpp
)

# Analysis sources
set(ANALYSIS_SOURCES
    src/analysis/DumpAnalyzer.cpp
    src/analysis/WinDbgAutomation.cpp
)

# Controller sources
set(CONTROLLER_SOURCES
    src/controllers/WppController.cpp
    src/controllers/PerfController.cpp
    src/controllers/EtwController.cpp
    src/controllers/PacketCaptureController.cpp
    src/controllers/ReproOrchestrator.cpp
)

# Output sources
set(OUTPUT_SOURCES
    src/output/ReportGenerator.cpp
    src/output/ZipPackager.cpp
)

# UI sources
set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/CollectorPanel.cpp
    src/ui/ControlPanel.cpp
    src/ui/TraceLevelPanel.cpp
    src/ui/ProgressDialog.cpp
    src/ui/ReproModeDialog.cpp
)

# Resource file
set(RC_FILE src/ui/Resources.rc)

add_executable(KT-WinDiagTool
    ${APP_SOURCES}
    ${COLLECTOR_SOURCES}
    ${ANALYSIS_SOURCES}
    ${CONTROLLER_SOURCES}
    ${OUTPUT_SOURCES}
    ${UI_SOURCES}
    ${RC_FILE}
)

target_include_directories(KT-WinDiagTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(KT-WinDiagTool PRIVATE
    nlohmann_json::nlohmann_json
    minizip_lib
    advapi32
    wevtapi
    tdh
    dbghelp
    pdh
    ole32
    oleaut32
    wbemuuid
    shlwapi
    shell32
    iphlpapi
    ws2_32
    dnsapi
    netapi32
    secur32
    ntdsapi
    version
)

# Embed application manifest requiring administrator
set_target_properties(KT-WinDiagTool PROPERTIES
    LINK_FLAGS "/ENTRY:wWinMainCRTStartup /MANIFESTUAC:\"level='asInvoker' uiAccess='false'\""
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Copy config files to output directory
add_custom_command(TARGET KT-WinDiagTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:KT-WinDiagTool>/config"
)

# Tests
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
