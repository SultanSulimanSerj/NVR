#include "nvr/system/HwMonitor.hpp"

#include "nvr/Logger.hpp"
#include "nvr/obs/Metrics.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace nvr::system {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

double readDoubleFromFile(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return 0.0;
    double v = 0;
    f >> v;
    return v;
}

double readCpuTempC() {
    double max_c = 0;
    for (int i = 0; i < 16; ++i) {
        auto v = readDoubleFromFile("/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp");
        if (v > 0) max_c = std::max(max_c, v / 1000.0);
    }
    return max_c;
}

}

HwMonitor::HwMonitor(EventBus* bus) : bus_(bus) {}
HwMonitor::~HwMonitor() { stop(); }

void HwMonitor::start() {
    if (run_.exchange(true)) return;
    t_ = std::thread([this] { loop(); });
}

void HwMonitor::stop() {
    if (!run_.exchange(false)) return;
    if (t_.joinable()) t_.join();
}

void HwMonitor::loop() {
    bool warned_hot = false;
    while (run_.load()) {
        auto cpu_c = readCpuTempC();
        obs::Registry::instance().global().archive_used_ratio;
        if (cpu_c > 0) {
            // expose as Prometheus-ish gauge via global metrics
            // (a dedicated CPU temp gauge can be added; reuse system labels)
        }

        if (cpu_c >= 85.0 && !warned_hot && bus_) {
            SystemEvent e;
            e.camera_id    = "system";
            e.type         = "hw.overheat";
            e.severity     = "warning";
            e.payload_json = json{{"cpu_temp_c", cpu_c}}.dump();
            bus_->publish(std::move(e));
            warned_hot = true;
        } else if (cpu_c < 75.0) {
            warned_hot = false;
        }

        for (int i = 0; i < 10 && run_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

}
