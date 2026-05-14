#include "nvr/PythonHookManager.hpp"

#include "nvr/Logger.hpp"

#include <chrono>
#include <filesystem>

#if NVR_WITH_PYTHON
#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
namespace py = pybind11;
#endif

namespace fs = std::filesystem;

namespace nvr {

struct PythonHookManager::Impl {
#if NVR_WITH_PYTHON
    std::unique_ptr<py::scoped_interpreter> interp;
    py::object                              module;
    py::object                              callable;
    bool                                    loaded{false};
#endif
};

PythonHookManager::PythonHookManager(PythonConfig cfg)
    : cfg_(std::move(cfg)),
      queue_(cfg_.queue_capacity),
      impl_(std::make_unique<Impl>()) {}

PythonHookManager::~PythonHookManager() { stop(); }

void PythonHookManager::start() {
    if (!cfg_.enabled) {
        NVR_INFO("python", "hooks disabled in config");
        return;
    }
    if (running_.exchange(true)) return;
    worker_ = std::thread(&PythonHookManager::workerLoop, this);
}

void PythonHookManager::stop() {
    if (!running_.exchange(false)) return;
    queue_.close();
    if (worker_.joinable()) worker_.join();
}

#if NVR_WITH_PYTHON

void PythonHookManager::workerLoop() {
    try {
        impl_->interp = std::make_unique<py::scoped_interpreter>();
    } catch (const std::exception& e) {
        NVR_ERROR("python", "scoped_interpreter failed: %s", e.what());
        running_ = false;
        return;
    }

    try {
        py::module_ sys = py::module_::import("sys");
        auto path_obj   = sys.attr("path").cast<py::list>();
        fs::path script_path(cfg_.script_path);
        path_obj.append(py::str(script_path.parent_path().string()));

        std::string mod_name = script_path.stem().string();
        impl_->module        = py::module_::import(mod_name.c_str());

        if (!py::hasattr(impl_->module, cfg_.callable.c_str())) {
            NVR_ERROR("python", "script '%s' has no callable '%s'",
                      cfg_.script_path.c_str(), cfg_.callable.c_str());
            running_ = false;
            return;
        }
        impl_->callable = impl_->module.attr(cfg_.callable.c_str());
        impl_->loaded   = true;
        NVR_INFO("python", "loaded script '%s', callable '%s'",
                 cfg_.script_path.c_str(), cfg_.callable.c_str());
    } catch (const std::exception& e) {
        NVR_ERROR("python", "failed to load script: %s", e.what());
        running_ = false;
        return;
    }

    while (running_.load()) {
        auto opt = queue_.waitAndPop();
        if (!opt) break;
        dispatch(*opt);
    }

    {
        py::gil_scoped_acquire gil;
        impl_->callable = py::object();
        impl_->module   = py::object();
    }
    impl_->interp.reset();
    NVR_INFO("python", "worker stopped (queue drops=%llu)",
             static_cast<unsigned long long>(queue_.droppedCount()));
}

void PythonHookManager::dispatch(const MotionEvent& ev) {
    if (!impl_ || !impl_->loaded) return;

    try {
        py::gil_scoped_acquire gil;

        auto t           = std::chrono::system_clock::to_time_t(ev.timestamp);
        std::tm tm{};
        localtime_r(&t, &tm);
        char iso[32];
        std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &tm);

        py::object frame = py::none();
        if (ev.include_frame && ev.width > 0 && ev.height > 0 && !ev.bgr_pixels.empty()) {
            std::vector<ssize_t> shape   = {ev.height, ev.width, ev.channels};
            std::vector<ssize_t> strides = {
                static_cast<ssize_t>(ev.width * ev.channels),
                static_cast<ssize_t>(ev.channels),
                1,
            };
            frame = py::array(py::buffer_info(
                const_cast<uint8_t*>(ev.bgr_pixels.data()),
                sizeof(uint8_t),
                py::format_descriptor<uint8_t>::format(),
                3, shape, strides));
        }

        py::object kwargs = py::dict();
        kwargs["clip_path"] = py::none();
        kwargs["roi_name"]  = py::none();
        impl_->callable(ev.camera_id,
                        std::string(iso),
                        ev.snapshot_path,
                        ev.area_ratio,
                        frame,
                        py::none(),
                        py::none());
    } catch (const std::exception& e) {
        NVR_WARN("python", "[%s] hook exception: %s", ev.camera_id.c_str(), e.what());
    }
}

#else

void PythonHookManager::workerLoop() {
    while (running_.load()) {
        auto opt = queue_.waitAndPop();
        if (!opt) break;
        NVR_INFO("python", "[stub] motion event: cam=%s area=%.4f snap=%s",
                 opt->camera_id.c_str(), opt->area_ratio, opt->snapshot_path.c_str());
    }
}

void PythonHookManager::dispatch(const MotionEvent& /*ev*/) {}

#endif

}
