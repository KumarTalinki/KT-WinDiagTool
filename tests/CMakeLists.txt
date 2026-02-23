include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Common test sources and includes
set(TEST_COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
)

# Test executable
add_executable(KT-WinDiagTool-Tests
    test_main.cpp
    test_config.cpp
    test_collectors_phase3.cpp
    test_collector_engine.cpp
    test_cli_handler.cpp
    test_env_var_collector.cpp
    test_registry_collector.cpp
    test_installed_products.cpp
    test_services_collector.cpp
    test_user_info_collector.cpp
    test_disk_space_collector.cpp
    test_proxy_collector.cpp
    test_network_config.cpp
    test_firewall_collector.cpp
    test_wer_collector.cpp
    test_system_info_collector.cpp
    test_product_log_collector.cpp
    test_install_log_collector.cpp
    test_eventlog_collector.cpp
    test_wpp_controller.cpp
    test_perf_controller.cpp
    test_packet_capture.cpp
    test_crash_dump_collector.cpp
    test_dump_analyzer.cpp
    test_windbg_automation.cpp
    test_repro_orchestrator.cpp
    test_zip_packager.cpp
    test_report_generator.cpp
    integration/test_full_collection.cpp
)

target_include_directories(KT-WinDiagTool-Tests PRIVATE
    ${TEST_COMMON_INCLUDES}
)

target_link_libraries(KT-WinDiagTool-Tests PRIVATE
    GTest::gtest
    GTest::gtest_main
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

# Link the application source files that tests need
target_sources(KT-WinDiagTool-Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/app/CLIHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/util/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/util/StringUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/util/Privileges.cpp
    ${CMAKE_SOURCE_DIR}/src/util/WmiHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/CollectorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/EnvVarCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/RegistryCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/InstalledProductsCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/ProductLogCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/InstallLogCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/ServicesCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/ProxySettingsCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/NetworkConfigCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/FirewallConfigCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/UserInfoCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/DiskSpaceCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/WerCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/SystemInfoCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/EventLogCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/EtwLogCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/WppLogCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/PerfLogCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/PacketCaptureCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/WppController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/PerfController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/EtwController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/PacketCaptureController.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/ReproOrchestrator.cpp
    ${CMAKE_SOURCE_DIR}/src/collectors/CrashDumpCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/analysis/DumpAnalyzer.cpp
    ${CMAKE_SOURCE_DIR}/src/analysis/WinDbgAutomation.cpp
    ${CMAKE_SOURCE_DIR}/src/output/ReportGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/output/ZipPackager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/CollectorPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ControlPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TraceLevelPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProgressDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ReproModeDialog.cpp
)

# Copy test config data
add_custom_command(TARGET KT-WinDiagTool-Tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:KT-WinDiagTool-Tests>/config"
)

include(GoogleTest)
gtest_discover_tests(KT-WinDiagTool-Tests)
