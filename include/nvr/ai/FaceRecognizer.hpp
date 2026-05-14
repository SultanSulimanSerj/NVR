#pragma once

#include "nvr/ai/IDetector.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nvr::store { class Database; }

namespace nvr::ai {

struct FaceMatch { int64_t person_id; std::string name; float similarity; };

class FaceRecognizer {
public:
    FaceRecognizer();
    ~FaceRecognizer();

    bool load(const std::string& embed_xml, const std::string& device = "CPU");
    bool ready() const noexcept;

    std::vector<float> embed(const uint8_t* bgr, int w, int h, int stride);

    void              setDatabase(store::Database* db) noexcept { db_ = db; }
    std::optional<FaceMatch> match(const std::vector<float>& emb, float threshold = 0.4f);
    bool              enroll(const std::string& name, const std::vector<float>& emb);

private:
    struct Impl;
    std::unique_ptr<Impl>  impl_;
    store::Database*       db_{nullptr};
};

}
