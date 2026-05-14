#pragma once

#include "nvr/Config.hpp"
#include "nvr/store/Crypto.hpp"
#include "nvr/store/Database.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace nvr::store {

enum class CameraChangeKind { Added, Updated, Removed };

struct CameraChange {
    CameraChangeKind kind;
    CameraConfig     camera;
};

class ConfigStore {
public:
    ConfigStore(Database& db, MasterKey master_key);

    void importFromYaml(const AppConfig& yaml_cfg);
    std::vector<CameraConfig> listCameras();
    std::optional<CameraConfig> getCamera(const std::string& id);
    void upsertCamera(const CameraConfig& cfg, const std::string& actor = "system");
    void deleteCamera(const std::string& id, const std::string& actor = "system");

    ArchiveConfig archiveConfig();
    void          setArchiveConfig(const ArchiveConfig& cfg);

    MotionConfig motionConfig();
    void         setMotionConfig(const MotionConfig& cfg);

    using CameraListener = std::function<void(const CameraChange&)>;
    void addCameraListener(CameraListener cb);

    Database& db() noexcept { return db_; }
    const MasterKey& masterKey() const noexcept { return key_; }

private:
    void  notify(const CameraChange& ch);
    CameraConfig rowToCamera(SQLite::Statement& q);

    Database&     db_;
    MasterKey     key_;
    std::mutex    listeners_mu_;
    std::vector<CameraListener> listeners_;
};

}
