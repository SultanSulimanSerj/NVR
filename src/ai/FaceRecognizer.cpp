#include "nvr/ai/FaceRecognizer.hpp"

#include "nvr/Logger.hpp"
#include "nvr/store/Database.hpp"

#include <openvino/openvino.hpp>

#include <cmath>

namespace nvr::ai {

struct FaceRecognizer::Impl {
    ov::Core            core;
    ov::CompiledModel   compiled;
    ov::InferRequest    req;
    int                 in_w{0}, in_h{0};
    bool                ready{false};
};

FaceRecognizer::FaceRecognizer() : impl_(std::make_unique<Impl>()) {}
FaceRecognizer::~FaceRecognizer() = default;

bool FaceRecognizer::ready() const noexcept { return impl_->ready; }

bool FaceRecognizer::load(const std::string& xml, const std::string& device) {
    try {
        auto m = impl_->core.read_model(xml);
        impl_->compiled = impl_->core.compile_model(m, device);
        impl_->req      = impl_->compiled.create_infer_request();
        auto sh = impl_->compiled.input().get_shape();
        impl_->in_h = static_cast<int>(sh[2]);
        impl_->in_w = static_cast<int>(sh[3]);
        impl_->ready = true;
        return true;
    } catch (const std::exception& e) {
        NVR_ERROR("ai.face", "load failed: %s", e.what());
        return false;
    }
}

std::vector<float> FaceRecognizer::embed(const uint8_t* bgr, int w, int h, int stride) {
    if (!impl_->ready) return {};
    auto input = impl_->compiled.input();
    ov::Tensor in(input.get_element_type(), input.get_shape());
    auto* dst = in.data<float>();
    for (int y = 0; y < impl_->in_h; ++y) {
        int sy = y * h / impl_->in_h;
        const uint8_t* row = bgr + sy * stride;
        for (int x = 0; x < impl_->in_w; ++x) {
            int sx = x * w / impl_->in_w;
            const uint8_t* p = row + sx * 3;
            int idx = (y * impl_->in_w + x);
            dst[idx]                                = p[0] / 255.f;
            dst[idx + impl_->in_w * impl_->in_h]     = p[1] / 255.f;
            dst[idx + 2 * impl_->in_w * impl_->in_h] = p[2] / 255.f;
        }
    }
    impl_->req.set_input_tensor(in);
    impl_->req.infer();
    auto t = impl_->req.get_output_tensor();
    const float* d = t.data<float>();
    size_t n = t.get_size();
    std::vector<float> v(d, d + n);
    float norm = 0.f;
    for (auto x : v) norm += x * x;
    norm = std::sqrt(norm);
    if (norm > 0) for (auto& x : v) x /= norm;
    return v;
}

std::optional<FaceMatch> FaceRecognizer::match(const std::vector<float>& emb, float threshold) {
    if (!db_ || emb.empty()) return std::nullopt;
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    SQLite::Statement q(db_->raw(), "SELECT id, name, embedding FROM face_persons");
    float           best = 0.f;
    FaceMatch       m{};
    while (q.executeStep()) {
        auto col = q.getColumn(2);
        if (col.isNull()) continue;
        const float* d = static_cast<const float*>(col.getBlob());
        size_t       n = col.getBytes() / sizeof(float);
        if (n != emb.size()) continue;
        float dot = 0.f;
        for (size_t i = 0; i < n; ++i) dot += d[i] * emb[i];
        if (dot > best) {
            best = dot;
            m.person_id = q.getColumn(0).getInt64();
            m.name      = q.getColumn(1).getString();
            m.similarity = dot;
        }
    }
    if (best < threshold) return std::nullopt;
    return m;
}

bool FaceRecognizer::enroll(const std::string& name, const std::vector<float>& emb) {
    if (!db_) return false;
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    SQLite::Statement q(db_->raw(),
        "INSERT INTO face_persons(name, embedding, created_at) VALUES(?, ?, datetime('now'))");
    q.bind(1, name);
    q.bind(2, emb.data(), static_cast<int>(emb.size() * sizeof(float)));
    return q.exec() > 0;
}

}
