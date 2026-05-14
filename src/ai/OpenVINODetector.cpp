#include "nvr/ai/OpenVINODetector.hpp"

#include "nvr/Logger.hpp"

#include <openvino/openvino.hpp>

#include <chrono>

namespace nvr::ai {

struct OpenVINODetector::Impl {
    ov::Core            core;
    ov::CompiledModel   compiled;
    ov::InferRequest    req;
    std::string         input_name;
    std::string         output_name;
    int                 input_w{0}, input_h{0};
};

OpenVINODetector::OpenVINODetector(std::string kind, std::vector<std::string> labels)
    : impl_(std::make_unique<Impl>()), kind_(std::move(kind)), labels_(std::move(labels)) {}

OpenVINODetector::~OpenVINODetector() = default;

bool OpenVINODetector::load(const std::string& model_xml, const std::string& device) {
    try {
        auto model      = impl_->core.read_model(model_xml);
        impl_->compiled = impl_->core.compile_model(model, device);
        impl_->req      = impl_->compiled.create_infer_request();
        auto input      = impl_->compiled.input();
        impl_->input_name = input.get_any_name();
        auto shape      = input.get_shape();
        if (shape.size() == 4) {
            impl_->input_h = static_cast<int>(shape[2]);
            impl_->input_w = static_cast<int>(shape[3]);
        }
        impl_->output_name = impl_->compiled.output().get_any_name();
        ready_ = true;
        NVR_INFO("ai", "OpenVINO model loaded: %s [%s] %dx%d",
                 model_xml.c_str(), device.c_str(), impl_->input_w, impl_->input_h);
        return true;
    } catch (const std::exception& e) {
        NVR_ERROR("ai", "OpenVINO load failed: %s", e.what());
        ready_ = false;
        return false;
    }
}

std::vector<Detection> OpenVINODetector::infer(const uint8_t* bgr, int w, int h, int stride) {
    std::vector<Detection> out;
    if (!ready_) return out;

    const auto t0 = std::chrono::steady_clock::now();
    try {
        auto input  = impl_->compiled.input();
        ov::Tensor in_tensor(input.get_element_type(), input.get_shape());

        const int iw = impl_->input_w;
        const int ih = impl_->input_h;
        auto* dst = in_tensor.data<float>();
        for (int y = 0; y < ih; ++y) {
            int sy = y * h / ih;
            const uint8_t* row = bgr + sy * stride;
            for (int x = 0; x < iw; ++x) {
                int sx = x * w / iw;
                const uint8_t* p = row + sx * 3;
                dst[(0 * ih + y) * iw + x] = p[0] / 255.f;
                dst[(1 * ih + y) * iw + x] = p[1] / 255.f;
                dst[(2 * ih + y) * iw + x] = p[2] / 255.f;
            }
        }
        impl_->req.set_input_tensor(in_tensor);
        impl_->req.infer();

        auto out_t  = impl_->req.get_output_tensor();
        auto out_sh = out_t.get_shape();
        const float* data = out_t.data<float>();
        size_t n   = out_sh.size() >= 2 ? out_sh[out_sh.size() - 2] : 0;
        size_t per = out_sh.size() >= 1 ? out_sh[out_sh.size() - 1] : 0;

        for (size_t i = 0; i < n; ++i) {
            const float* d = data + i * per;
            float conf = (per >= 7) ? d[2] : (per >= 5 ? d[4] : 0);
            if (conf < conf_thr_) continue;
            Detection det;
            det.confidence = conf;
            if (per >= 7) {
                int cls = static_cast<int>(d[1]);
                det.class_name = (cls >= 0 && static_cast<size_t>(cls) < labels_.size())
                                  ? labels_[cls] : kind_;
                det.bbox.x = d[3];
                det.bbox.y = d[4];
                det.bbox.w = d[5] - d[3];
                det.bbox.h = d[6] - d[4];
            } else {
                det.class_name = kind_;
                det.bbox.x = d[0] / w;
                det.bbox.y = d[1] / h;
                det.bbox.w = (d[2] - d[0]) / w;
                det.bbox.h = (d[3] - d[1]) / h;
            }
            det.ts = std::chrono::system_clock::now();
            out.push_back(det);
        }
    } catch (const std::exception& e) {
        NVR_WARN("ai", "inference error: %s", e.what());
    }
    last_ms_ = std::chrono::duration<float, std::milli>(
                  std::chrono::steady_clock::now() - t0).count();
    return out;
}

}
