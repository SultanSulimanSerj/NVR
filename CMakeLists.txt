cmake_minimum_required(VERSION 3.18)
project(nvr_prototype LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NVR_WITH_PYTHON         "Embed CPython for analytics hooks"  ON)
option(NVR_WITH_OPENCV         "Enable OpenCV motion detection"     ON)
option(NVR_WITH_OPENVINO       "Enable OpenVINO inference"          OFF)
option(NVR_BUILD_TESTS         "Build unit tests"                   OFF)
option(NVR_BUILD_KIOSK_SHELL   "Build Rust nvr-shell kiosk client"  OFF)
option(NVR_HARDEN              "Stack/heap/RELRO hardening"         ON)
option(NVR_WARNINGS_AS_ERRORS  "Treat warnings as errors (own targets only)" OFF)
option(NVR_ENABLE_ASAN         "AddressSanitizer (debug builds)"    OFF)
option(NVR_ENABLE_UBSAN        "UndefinedBehaviorSanitizer"         OFF)
option(NVR_ENABLE_TSAN         "ThreadSanitizer (mutually exclusive with ASAN)" OFF)

if(NVR_ENABLE_ASAN AND NVR_ENABLE_TSAN)
    message(FATAL_ERROR "NVR_ENABLE_ASAN and NVR_ENABLE_TSAN are mutually exclusive")
endif()

# Project-wide warnings — applied via the `nvr_warnings` interface library below
# so we only target our own code (fetched deps stay clean).
add_library(nvr_warnings INTERFACE)
target_compile_options(nvr_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -fno-omit-frame-pointer
)
if(NVR_WARNINGS_AS_ERRORS)
    target_compile_options(nvr_warnings INTERFACE -Werror)
endif()

# Hardening: defensive against memory-corruption bugs and LD_PRELOAD attacks.
# All flags are GCC/Clang on Linux; we silently skip them elsewhere.
add_library(nvr_hardening INTERFACE)
if(NVR_HARDEN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nvr_hardening INTERFACE
        -fstack-protector-strong
        -fstack-clash-protection
        -fcf-protection=full
        $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>
        $<$<CONFIG:Release>:-D_GLIBCXX_ASSERTIONS>
    )
    target_link_options(nvr_hardening INTERFACE
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
        -pie
    )
endif()

add_library(nvr_sanitizers INTERFACE)
if(NVR_ENABLE_ASAN)
    target_compile_options(nvr_sanitizers INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options   (nvr_sanitizers INTERFACE -fsanitize=address)
endif()
if(NVR_ENABLE_UBSAN)
    target_compile_options(nvr_sanitizers INTERFACE
        -fsanitize=undefined -fno-sanitize-recover=undefined)
    target_link_options   (nvr_sanitizers INTERFACE -fsanitize=undefined)
endif()
if(NVR_ENABLE_TSAN)
    target_compile_options(nvr_sanitizers INTERFACE -fsanitize=thread)
    target_link_options   (nvr_sanitizers INTERFACE -fsanitize=thread)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

if(NVR_WITH_OPENCV)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs video)
endif()

if(NVR_WITH_OPENVINO)
    find_package(OpenVINO QUIET COMPONENTS Runtime)
    if(NOT OpenVINO_FOUND)
        message(WARNING "OpenVINO not found; disabling NVR_WITH_OPENVINO")
        set(NVR_WITH_OPENVINO OFF CACHE BOOL "" FORCE)
    else()
        add_compile_definitions(NVR_WITH_OPENVINO=1)
    endif()
endif()

if(NVR_WITH_PYTHON)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter Development NumPy)
    find_package(pybind11 CONFIG)
    if(NOT pybind11_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG        v2.13.6
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
endif()

find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.14.1
    )
    set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)
endif()

find_package(SQLiteCpp QUIET)
if(NOT SQLiteCpp_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        SQLiteCpp
        GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
        GIT_TAG        3.3.2
    )
    set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_INTERNAL_SQLITE ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SQLiteCpp)
endif()

find_package(CURL REQUIRED)

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

include(FetchContent)
FetchContent_Declare(
    crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        v1.2.1
)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CROW_AMALGAMATE     OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(crow)

FetchContent_Declare(
    jwtcpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG        v0.7.0
)
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(jwtcpp)

find_package(PkgConfig)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

set(NVR_SOURCES
    src/main.cpp
    src/core/Config.cpp
    src/core/RecordingSchedule.cpp
    src/core/LicenseGate.cpp
    src/core/Logger.cpp
    src/core/CameraSupervisor.cpp
    src/core/EventBus.cpp
    src/archive/ArchiveManager.cpp
    src/camera/CameraPipeline.cpp
    src/camera/FFmpegDecoder.cpp
    src/camera/FFmpegEncoder.cpp
    src/camera/SubStreamEncoder.cpp
    src/camera/HlsMuxer.cpp
    src/camera/PreEventBuffer.cpp
    src/analytics/MotionDetector.cpp
    src/analytics/AnalyticsWorker.cpp
    src/python/PythonHookManager.cpp
    src/store/Database.cpp
    src/store/ConfigStore.cpp
    src/store/Crypto.cpp
    src/obs/Metrics.cpp
    src/api/HttpServer.cpp
    src/api/HttpRouteHelpers.cpp
    src/api/RouteArchiveEvents.cpp
    src/api/RouteAuth.cpp
    src/api/RouteCamerasLicense.cpp
    src/api/RouteUsersAudit.cpp
    src/api/RouteConfig.cpp
    src/api/RouteNotifications.cpp
    src/api/RouteInfra.cpp
    src/api/RouteLiveLayouts.cpp
    src/api/RouteApplication.cpp
    src/api/RouteStream.cpp
    src/api/Auth.cpp
    src/onvif/WsDiscovery.cpp
    src/onvif/SoapClient.cpp
    src/onvif/MediaService.cpp
    src/onvif/PtzService.cpp
    src/onvif/EventsPull.cpp
    src/onvif/EnumerateStreams.cpp
    src/notify/NotificationManager.cpp
    src/notify/EmailChannel.cpp
    src/notify/TelegramChannel.cpp
    src/notify/WebhookChannel.cpp
    src/notify/MqttChannel.cpp
    src/notify/MqttTcp311.cpp
    src/notify/SyslogChannel.cpp
    src/ai/Tracker.cpp
    src/ai/AiPipeline.cpp
    src/system/HwMonitor.cpp
)

if(NVR_WITH_OPENVINO)
    list(APPEND NVR_SOURCES
        src/ai/OpenVINODetector.cpp
        src/ai/FaceRecognizer.cpp)
endif()

add_executable(nvr_prototype ${NVR_SOURCES})

target_include_directories(nvr_prototype
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(nvr_prototype
    PRIVATE
        nvr_warnings
        nvr_hardening
        nvr_sanitizers
        PkgConfig::FFMPEG
        PkgConfig::SODIUM
        Threads::Threads
        yaml-cpp::yaml-cpp
        spdlog::spdlog
        SQLiteCpp
        sqlite3
        nlohmann_json::nlohmann_json
        Crow::Crow
        jwt-cpp::jwt-cpp
        CURL::libcurl
)

if(NVR_WITH_OPENVINO)
    target_link_libraries(nvr_prototype PRIVATE openvino::runtime)
endif()

if(NVR_WITH_OPENCV)
    target_link_libraries(nvr_prototype PRIVATE ${OpenCV_LIBS})
    target_include_directories(nvr_prototype PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(nvr_prototype PRIVATE NVR_WITH_OPENCV=1)
endif()

if(NVR_WITH_PYTHON)
    target_link_libraries(nvr_prototype PRIVATE pybind11::embed Python3::NumPy)
    target_compile_definitions(nvr_prototype PRIVATE NVR_WITH_PYTHON=1)
endif()

option(NVR_BUILD_LICENSE_TOOL "Build nvr_license_sign (vendor-only offline signer)" OFF)

add_executable(nvr_cli src/cli/nvr_cli.cpp)
target_include_directories(nvr_cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nvr_cli PRIVATE
    nvr_warnings nvr_hardening nvr_sanitizers
    Threads::Threads nlohmann_json::nlohmann_json CURL::libcurl)

if(NVR_BUILD_LICENSE_TOOL)
    add_executable(nvr_license_sign tools/nvr_license_sign.cpp)
    target_include_directories(nvr_license_sign PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(nvr_license_sign PRIVATE PkgConfig::SODIUM nlohmann_json::nlohmann_json)
endif()

install(TARGETS nvr_prototype nvr_cli RUNTIME DESTINATION bin)
if(NVR_BUILD_LICENSE_TOOL)
    install(TARGETS nvr_license_sign RUNTIME DESTINATION bin)
endif()
install(DIRECTORY config/ DESTINATION etc/nvr-prototype FILES_MATCHING PATTERN "*.yaml")
install(FILES scripts/script.py DESTINATION share/nvr-prototype/scripts)
install(FILES deploy/logrotate/nvr-prototype-hooks DESTINATION share/nvr-prototype/logrotate)

# Operator-facing scripts go into /usr/bin so the service units (and any
# admin run) can find them at a stable path regardless of source-vs-deb install.
install(PROGRAMS
    "${CMAKE_SOURCE_DIR}/scripts/nvr_backup.sh"
    "${CMAKE_SOURCE_DIR}/scripts/nvr_restore.sh"
    "${CMAKE_SOURCE_DIR}/scripts/nvr_recover.sh"
    "${CMAKE_SOURCE_DIR}/scripts/nvr_update.sh"
    "${CMAKE_SOURCE_DIR}/scripts/nvr_db_check.sh"
    "${CMAKE_SOURCE_DIR}/scripts/nvr_archive_compress.sh"
    "${CMAKE_SOURCE_DIR}/scripts/nvr_sync_nginx_archive_accel.sh"
    DESTINATION bin)

# Kiosk helpers stay under /usr/share since they're launched by systemd units
# that already reference that path.
install(PROGRAMS
    "${CMAKE_SOURCE_DIR}/scripts/nvr_kiosk_launcher.sh"
    "${CMAKE_SOURCE_DIR}/scripts/nvr_kiosk_bootstrap.sh"
    DESTINATION share/nvr-prototype/scripts)

# Drop a default branding.json so /api/v1/branding has something to serve
# before a customer edits it.
if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/branding.json")
    install(FILES deploy/branding.json DESTINATION etc/nvr-prototype)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/camera_catalog.sample.json")
    install(FILES deploy/camera_catalog.sample.json DESTINATION share/nvr-prototype)
endif()
install(FILES deploy/nginx/nginx-archive-accel.conf DESTINATION etc/nvr-prototype)

install(FILES deploy/nvr-prototype.service          DESTINATION lib/systemd/system)
if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/apparmor/usr.bin.nvr_prototype")
    install(FILES deploy/apparmor/usr.bin.nvr_prototype DESTINATION etc/apparmor.d)
endif()
install(FILES deploy/nvr-db-check.service           DESTINATION lib/systemd/system OPTIONAL)
install(FILES deploy/nvr-archive-compress.service   DESTINATION lib/systemd/system OPTIONAL)
install(FILES deploy/nvr-archive-compress.timer     DESTINATION lib/systemd/system OPTIONAL)
install(FILES deploy/nvr-kiosk.service              DESTINATION lib/systemd/system OPTIONAL)
install(FILES deploy/nvr-prototype.sysctl.conf      DESTINATION lib/sysctl.d        OPTIONAL)
install(DIRECTORY webui/dist/                       DESTINATION share/nvr-prototype/webui OPTIONAL)

if(NVR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NVR_BUILD_KIOSK_SHELL)
    find_program(CARGO_EXECUTABLE cargo REQUIRED DOC "Rust toolchain (cargo)")
    set(NVR_SHELL_MANIFEST "${CMAKE_SOURCE_DIR}/nvr-shell/Cargo.toml")
    set(NVR_SHELL_OUT "${CMAKE_SOURCE_DIR}/nvr-shell/target/release/nvr-shell")
    set(NVR_SHELL_BIN "${CMAKE_BINARY_DIR}/nvr-shell/nvr-shell")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/nvr-shell")
    add_custom_command(
        OUTPUT "${NVR_SHELL_BIN}"
        COMMAND ${CARGO_EXECUTABLE} build --release --manifest-path "${NVR_SHELL_MANIFEST}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NVR_SHELL_OUT}" "${NVR_SHELL_BIN}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/nvr-shell"
        COMMENT "Building nvr-shell (Rust)"
        DEPENDS "${CMAKE_SOURCE_DIR}/nvr-shell/Cargo.toml"
                "${CMAKE_SOURCE_DIR}/nvr-shell/src/main.rs"
        VERBATIM)
    add_custom_target(nvr-shell-rust DEPENDS "${NVR_SHELL_BIN}")
    add_dependencies(nvr_prototype nvr-shell-rust)
    install(PROGRAMS "${NVR_SHELL_BIN}" DESTINATION bin)
endif()

set(CPACK_GENERATOR                  "DEB")
set(CPACK_PACKAGE_NAME               "nvr-prototype")
set(CPACK_PACKAGE_VERSION            "0.2.0")
set(CPACK_PACKAGE_CONTACT            "nvr-team@example.invalid")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER  "NVR Team <nvr-team@example.invalid>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libavformat59 | libavformat58, libavcodec59 | libavcodec58, libswscale6 | libswscale5, libopencv-core, libsodium23, libsqlite3-0, libcurl4, python3, python3-numpy"
)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "cage, seatd, chromium-browser | chromium")
if(NVR_BUILD_KIOSK_SHELL)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libwebkit2gtk-4.1-0, libgtk-3-0, libjavascriptcoregtk-4.1-0")
endif()
set(CPACK_DEBIAN_PACKAGE_SECTION     "video")
set(CPACK_DEBIAN_PACKAGE_PRIORITY    "optional")
set(CPACK_DEBIAN_FILE_NAME           DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/deploy/debian/postinst;${CMAKE_SOURCE_DIR}/deploy/debian/prerm")
include(CPack)

message(STATUS "NVR configuration:")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard   : ${CMAKE_CXX_STANDARD}")
message(STATUS "  OpenCV         : ${NVR_WITH_OPENCV}")
message(STATUS "  Kiosk shell    : ${NVR_BUILD_KIOSK_SHELL}")
