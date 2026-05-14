include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(NVR_TEST_LIBS
    GTest::gtest_main
    PkgConfig::FFMPEG
    Threads::Threads
    yaml-cpp::yaml-cpp
    spdlog::spdlog
    SQLiteCpp
    sqlite3
    PkgConfig::SODIUM
    nlohmann_json::nlohmann_json
)

add_executable(nvr_tests
    test_thread_safe_queue.cpp
    test_archive_eviction.cpp
    test_config_yaml.cpp
    test_config_archive.cpp
    test_config_store.cpp
    test_crypto.cpp
    test_motion_roi.cpp
    test_recording_muxer_policy.cpp
    test_recording_schedule.cpp
    test_onvif_xml.cpp
    test_database_migrations.cpp
    test_license_gate.cpp
    ../src/core/LicenseGate.cpp
    ../src/core/Logger.cpp
    ../src/core/Config.cpp
    ../src/core/RecordingSchedule.cpp
    ../src/core/EventBus.cpp
    ../src/archive/ArchiveManager.cpp
    ../src/store/Database.cpp
    ../src/store/ConfigStore.cpp
    ../src/store/Crypto.cpp
    ../src/obs/Metrics.cpp
    ../src/analytics/MotionDetector.cpp
)
target_include_directories(nvr_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(nvr_tests PRIVATE ${NVR_TEST_LIBS})

include(GoogleTest)
gtest_discover_tests(nvr_tests)
