#include "nvr/AnalyticsWorker.hpp"
#include "nvr/ArchiveManager.hpp"
#include "nvr/CameraSupervisor.hpp"
#include "nvr/Config.hpp"
#include "nvr/LicenseGate.hpp"
#include "nvr/EventBus.hpp"
#include "nvr/Logger.hpp"
#include "nvr/PythonHookManager.hpp"
#include "nvr/api/Auth.hpp"
#include "nvr/api/HttpServer.hpp"
#include "nvr/notify/NotificationManager.hpp"
#include "nvr/store/ConfigStore.hpp"
#include "nvr/store/Crypto.hpp"
#include "nvr/store/Database.hpp"
#include "nvr/system/HwMonitor.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <poll.h>

namespace {
void sd_notify_str(const char* state) {
    const char* path = std::getenv("NOTIFY_SOCKET");
    if (!path || !*path) return;
    int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path[0] == '@') {
        addr.sun_path[0] = 0;
        std::snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "%s", path + 1);
    } else {
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    }
    ::sendto(fd, state, std::strlen(state), 0,
              reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(fd);
}

// Parse the systemd WATCHDOG_USEC env var (microseconds). Returns 0 if disabled.
unsigned long long watchdog_period_us() {
    const char* w = std::getenv("WATCHDOG_USEC");
    if (!w || !*w) return 0;
    try { return std::stoull(w); } catch (...) { return 0; }
}
}
#else
namespace {
void sd_notify_str(const char*) {}
unsigned long long watchdog_period_us() { return 0; }
}
#endif

namespace {

nvr::LogLevel parseLogLevel(const std::string& s) {
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "trace") return nvr::LogLevel::Trace;
    if (v == "debug") return nvr::LogLevel::Debug;
    if (v == "warn" || v == "warning") return nvr::LogLevel::Warn;
    if (v == "error") return nvr::LogLevel::Error;
    return nvr::LogLevel::Info;
}

// Block SIGINT/SIGTERM/SIGPIPE/SIGHUP in all threads and pump them through
// signalfd on the main thread. Eliminates the async-signal-safe minefield of
// std::signal handlers touching atomics from arbitrary threads.
#ifdef __linux__
int makeSignalfd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) return -1;
    return ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
}
#endif

// Tiny RAII helpers — each module gets started in order and stopped in reverse
// by destructors. If something throws mid-bring-up, only the already-started
// pieces are stopped.
struct ScopeGuard {
    std::function<void()> f;
    ~ScopeGuard() { if (f) try { f(); } catch (...) {} }
};

}

int main(int argc, char** argv) {
    // Two invocation forms:
    //   nvr_prototype <config.yaml>                — normal daemon
    //   nvr_prototype --migrate-only <config.yaml> — run DB migrations and exit
    // The second form is used by `postinst` so an upgrade can fail loudly when
    // a migration is broken instead of catching it on first restart.
    bool migrate_only = false;
    const char* cfg_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--migrate-only") migrate_only = true;
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: " << argv[0]
                      << " [--migrate-only] <config.yaml>\n";
            return EXIT_SUCCESS;
        }
        else if (!cfg_path) cfg_path = argv[i];
    }
    if (!cfg_path) {
        std::cerr << "Usage: " << argv[0]
                  << " [--migrate-only] <config.yaml>\n";
        return EXIT_FAILURE;
    }

    nvr::AppConfig cfg;
    try {
        cfg = nvr::loadConfig(cfg_path);
    } catch (const std::exception& e) {
        std::cerr << "config error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    if (migrate_only) {
        try {
            nvr::Logger::instance().initialize(cfg.logging_file);
            nvr::store::loadOrCreateMasterKey(cfg.database.master_key_file);
            nvr::store::Database db(cfg.database.path);
            std::cout << "Migrations applied; schema is up to date.\n";
            return EXIT_SUCCESS;
        } catch (const std::exception& e) {
            std::cerr << "migrate-only failed: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    nvr::Logger::instance().initialize(cfg.logging_file);
    nvr::Logger::instance().setLevel(parseLogLevel(cfg.logging_level));
    nvr::installFfmpegLogBridge();
    NVR_INFO("main", "nvr_prototype starting");

#ifdef __linux__
    int sigfd = makeSignalfd();
    if (sigfd < 0) {
        NVR_ERROR("main", "signalfd init failed; falling back to std::signal");
    }
#else
    int sigfd = -1;
#endif

    std::vector<ScopeGuard> rollback;

    nvr::store::MasterKey master_key;
    try {
        master_key = nvr::store::loadOrCreateMasterKey(cfg.database.master_key_file);
    } catch (const std::exception& e) {
        NVR_ERROR("main", "master key error: %s", e.what());
        return EXIT_FAILURE;
    }

    nvr::store::Database    db(cfg.database.path);
    nvr::store::ConfigStore store(db, master_key);
    store.importFromYaml(cfg);

    auto archive_cfg = store.archiveConfig();
    if (archive_cfg.root_path.empty()) archive_cfg = cfg.archive;
    nvr::ArchiveManager archive(archive_cfg);
    archive.setOnSegmentFinalized([&db](const nvr::SegmentInfo& s) {
        auto fmt = [](std::chrono::system_clock::time_point tp) {
            auto t = std::chrono::system_clock::to_time_t(tp);
            std::tm tm{};
            gmtime_r(&t, &tm);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
            return std::string(buf);
        };
        auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          s.ended_at - s.started_at).count();
        try {
            db.insertSegment(s.camera_id, s.path.string(),
                              fmt(s.started_at), fmt(s.ended_at),
                              static_cast<int64_t>(dur_ms), s.size_bytes, s.has_motion);
        } catch (const std::exception& e) {
            NVR_WARN("segments", "insert failed: %s", e.what());
        }
    });
    archive.setOnSegmentEvicted([&db](const std::filesystem::path& p) {
        try { db.deleteSegmentByPath(p.string()); } catch (...) {}
    });
    try {
        archive.start();
        rollback.push_back({[&] { archive.stop(); }});
    } catch (const std::exception& e) {
        NVR_ERROR("main", "archive.start failed: %s", e.what());
        return EXIT_FAILURE;
    }

    nvr::PythonHookManager hooks(cfg.python);
    try {
        hooks.start();
        rollback.push_back({[&] { hooks.stop(); }});
    } catch (const std::exception& e) {
        NVR_ERROR("main", "hooks.start failed: %s", e.what());
        return EXIT_FAILURE;
    }

    nvr::LicenseGate license_gate(cfg.license_paths);
    if (cfg.cameras.size() >
        static_cast<size_t>(license_gate.effectiveMaxChannels())) {
        NVR_WARN("main",
                 "YAML defines %zu cameras but license allows %d; add cameras via API will be capped",
                 cfg.cameras.size(), license_gate.effectiveMaxChannels());
    }

    nvr::EventBus bus;

    nvr::AnalyticsWorker analytics_worker;
    analytics_worker.setEventBus(&bus);
    analytics_worker.setDatabase(&db);
    analytics_worker.setLicenseGate(&license_gate);
    analytics_worker.start();
    rollback.push_back({[&] { analytics_worker.stop(); }});

    nvr::CameraSupervisor supervisor(store, &archive, &hooks.queue(), &bus,
                                      cfg.python.include_frame, cfg.http.hls_root);
    try {
        supervisor.start();
        rollback.push_back({[&] { supervisor.stop(); }});
    } catch (const std::exception& e) {
        NVR_ERROR("main", "supervisor.start failed: %s", e.what());
        return EXIT_FAILURE;
    }

    const int access_s  = std::max(60, cfg.http.jwt_access_ttl_seconds);
    const int refresh_s = std::max(300, cfg.http.jwt_refresh_ttl_seconds);
    nvr::api::Auth auth(db,
                         nvr::api::Auth::loadOrCreateSecret(cfg.http.jwt_secret_file),
                         master_key,
                         std::chrono::seconds(access_s),
                         std::chrono::seconds(refresh_s));
    auth.ensureFirstRunAdmin();

    nvr::notify::NotificationManager notify_mgr(store, bus);
    try {
        notify_mgr.start();
        rollback.push_back({[&] { notify_mgr.stop(); }});
    } catch (const std::exception& e) {
        NVR_ERROR("main", "notify_mgr.start failed: %s", e.what());
        return EXIT_FAILURE;
    }

    nvr::system::HwMonitor hwmon(&bus);
    try {
        hwmon.start();
        rollback.push_back({[&] { hwmon.stop(); }});
    } catch (...) {
        NVR_WARN("main", "hwmon failed to start; continuing");
    }

    nvr::api::HttpServer http(cfg.http, store, auth, &supervisor, &bus, &notify_mgr, license_gate);
    try {
        http.start();
        rollback.push_back({[&] { http.stop(); }});
    } catch (const std::exception& e) {
        NVR_ERROR("main", "http.start failed: %s", e.what());
        return EXIT_FAILURE;
    }

    sd_notify_str("READY=1\nSTATUS=running");

    // Watchdog: half the configured WATCHDOG_USEC, capped to 30s.
    auto wd_us = watchdog_period_us();
    auto wd_interval = std::chrono::milliseconds(
        wd_us ? std::min<uint64_t>(wd_us / 2 / 1000, 30000) : 10000);

    auto watchdog_tick = std::chrono::steady_clock::now();
    auto next_db_cleanup = std::chrono::steady_clock::now() + std::chrono::hours(1);
    bool stop = false;
    while (!stop) {
#ifdef __linux__
        if (sigfd >= 0) {
            pollfd pf{sigfd, POLLIN, 0};
            int rv = ::poll(&pf, 1, static_cast<int>(wd_interval.count()));
            if (rv > 0 && (pf.revents & POLLIN)) {
                signalfd_siginfo si{};
                while (::read(sigfd, &si, sizeof(si)) == static_cast<ssize_t>(sizeof(si))) {
                    if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM) {
                        NVR_INFO("main", "signal %u received", si.ssi_signo);
                        stop = true;
                    } else if (si.ssi_signo == SIGHUP) {
                        NVR_INFO("main", "SIGHUP — reloading notifications");
                        try { notify_mgr.reload(); } catch (...) {}
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(wd_interval);
        }
#else
        std::this_thread::sleep_for(wd_interval);
#endif

        auto now = std::chrono::steady_clock::now();
        if (now >= next_db_cleanup) {
            try { db.purgeExpiredAuthData(); } catch (...) {}
            next_db_cleanup = now + std::chrono::hours(1);
        }
        if (now - watchdog_tick >= wd_interval) {
            sd_notify_str("WATCHDOG=1");
            watchdog_tick = now;
        }
    }

    sd_notify_str("STOPPING=1");
    NVR_INFO("main", "shutting down");

    // Shutdown order: 1) HTTP (refuse new requests), 2) supervisor (drain
    // segments), 3) notify (flush queue), 4) hwmon, 5) hooks, 6) archive.
    try { http.stop();        } catch (...) {}
    try { supervisor.stop();  } catch (...) {}
    try { notify_mgr.stop();  } catch (...) {}
    try { hwmon.stop();       } catch (...) {}
    try { hooks.stop();       } catch (...) {}
    try { archive.stop();     } catch (...) {}

    // Cancel rollback guards since we shut down cleanly above.
    rollback.clear();

#ifdef __linux__
    if (sigfd >= 0) ::close(sigfd);
#endif
    NVR_INFO("main", "shutdown complete");
    return EXIT_SUCCESS;
}
