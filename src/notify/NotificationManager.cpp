#include "nvr/notify/NotificationManager.hpp"

#include "nvr/Logger.hpp"
#include "nvr/obs/Metrics.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <algorithm>

namespace nvr::notify {

std::unique_ptr<INotificationChannel> makeEmail   (const nlohmann::json&);
std::unique_ptr<INotificationChannel> makeTelegram(const nlohmann::json&);
std::unique_ptr<INotificationChannel> makeWebhook (const nlohmann::json&);
std::unique_ptr<INotificationChannel> makeMqtt    (const nlohmann::json&, store::ConfigStore*);
std::unique_ptr<INotificationChannel> makeSyslog   (const nlohmann::json&);

std::unique_ptr<INotificationChannel> makeChannel(const std::string& kind,
                                                  const nlohmann::json& cfg,
                                                  store::ConfigStore&     store) {
    if (kind == "email")    return makeEmail   (cfg);
    if (kind == "telegram") return makeTelegram(cfg);
    if (kind == "webhook")  return makeWebhook (cfg);
    if (kind == "mqtt")     return makeMqtt    (cfg, &store);
    if (kind == "syslog")   return makeSyslog  (cfg);
    NVR_WARN("notify", "unknown channel kind: %s", kind.c_str());
    return nullptr;
}

NotificationManager::NotificationManager(store::ConfigStore& store, EventBus& bus,
                                          std::size_t worker_count, std::size_t max_queue)
    : store_(store), bus_(bus),
      worker_count_(std::max<std::size_t>(1, worker_count)),
      max_queue_(std::max<std::size_t>(16, max_queue)) {}

NotificationManager::~NotificationManager() { stop(); }

void NotificationManager::start() {
    if (running_.exchange(true)) return;
    reload();
    workers_.reserve(worker_count_);
    for (std::size_t i = 0; i < worker_count_; ++i)
        workers_.emplace_back(&NotificationManager::workerLoop, this);
    sub_id_ = bus_.subscribe([this](const SystemEvent& ev) { dispatch(ev); });
    NVR_INFO("notify", "started: %zu worker(s), queue cap %zu", worker_count_, max_queue_);
}

void NotificationManager::stop() {
    if (!running_.exchange(false)) return;
    if (sub_id_) { bus_.unsubscribe(sub_id_); sub_id_ = 0; }
    {
        std::lock_guard<std::mutex> lk(q_mu_);
        queue_.clear();
    }
    q_cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
}

void NotificationManager::reload() {
    std::lock_guard<std::mutex> lk(cfg_mu_);
    // Channels are shared_ptr — workers holding an old pointer drain their
    // current job and then release. The next dispatch picks up the new map.
    channels_.clear();
    rules_.clear();
    try {
        std::lock_guard<std::recursive_mutex> dlk(store_.db().mutex());
        {
            SQLite::Statement q(store_.db().raw(),
                "SELECT id, kind, name, config_enc, enabled FROM notification_channels");
            while (q.executeStep()) {
                if (q.getColumn(4).getInt() == 0) continue;
                auto blob_col = q.getColumn(3);
                std::string cfg_raw;
                if (!blob_col.isNull()) {
                    std::vector<uint8_t> blob(
                        static_cast<const uint8_t*>(blob_col.getBlob()),
                        static_cast<const uint8_t*>(blob_col.getBlob()) + blob_col.getBytes());
                    auto plain = store::decrypt(store_.masterKey(), blob,
                                                "notif:" + std::to_string(q.getColumn(0).getInt64()));
                    cfg_raw = plain.value_or("{}");
                }
                nlohmann::json cfg = cfg_raw.empty() ? nlohmann::json::object()
                                                      : nlohmann::json::parse(cfg_raw, nullptr, false);
                if (cfg.is_discarded()) cfg = nlohmann::json::object();
                std::shared_ptr<INotificationChannel> ch(
                    makeChannel(q.getColumn(1).getString(), cfg, store_).release());
                if (!ch) continue;
                ChannelEntry e;
                e.id      = q.getColumn(0).getInt64();
                e.kind    = q.getColumn(1).getString();
                e.name    = q.getColumn(2).getString();
                e.channel = std::move(ch);
                channels_[e.id] = std::move(e);
            }
        }
        {
            SQLite::Statement q(store_.db().raw(),
                "SELECT id, camera_id, event_type, severity_min, throttle_seconds, channel_id "
                "FROM notification_rules");
            while (q.executeStep()) {
                Rule r;
                r.id               = q.getColumn(0).getInt64();
                r.camera_id        = q.getColumn(1).isNull() ? "" : q.getColumn(1).getString();
                r.event_type       = q.getColumn(2).getString();
                r.severity_min     = q.getColumn(3).getString();
                r.throttle_seconds = q.getColumn(4).getInt();
                r.channel_id       = q.getColumn(5).getInt64();
                rules_.push_back(std::move(r));
            }
        }
    } catch (const std::exception& e) {
        NVR_ERROR("notify", "reload failed: %s", e.what());
    }
    NVR_INFO("notify", "loaded %zu channel(s), %zu rule(s)",
             channels_.size(), rules_.size());
}

std::size_t NotificationManager::queueDepth() {
    std::lock_guard<std::mutex> lk(q_mu_);
    return queue_.size();
}

int NotificationManager::severityRank(const std::string& s) const {
    if (s == "critical") return 3;
    if (s == "error")    return 2;
    if (s == "warning")  return 1;
    return 0;
}

void NotificationManager::dispatch(const SystemEvent& ev) {
    if (!running_.load()) return;
    std::vector<DeliveryJob> jobs;
    {
        std::lock_guard<std::mutex> lk(cfg_mu_);
        auto now = std::chrono::steady_clock::now();
        for (auto& r : rules_) {
            if (!r.camera_id.empty() && r.camera_id != ev.camera_id) continue;
            if (r.event_type != "*" && r.event_type != ev.type)       continue;
            if (severityRank(ev.severity) < severityRank(r.severity_min)) continue;

            auto key = std::to_string(r.id) + "|" + ev.camera_id + "|" + ev.type;
            auto it  = last_sent_.find(key);
            if (it != last_sent_.end() &&
                std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count()
                    < r.throttle_seconds) continue;
            last_sent_[key] = now;

            auto cit = channels_.find(r.channel_id);
            if (cit == channels_.end()) continue;
            DeliveryJob j;
            j.channel_id = cit->second.id;
            j.rule_id    = r.id;
            j.channel    = cit->second.channel;
            j.event      = ev;
            j.not_before = now;
            jobs.push_back(std::move(j));
        }
    }

    if (jobs.empty()) return;
    auto& global_m = obs::Registry::instance().global();
    std::size_t enqueued = 0;
    {
        std::lock_guard<std::mutex> lk(q_mu_);
        for (auto& j : jobs) {
            if (queue_.size() >= max_queue_) {
                global_m.notifications_dropped_total.inc();
                NVR_WARN("notify", "queue full (%zu), dropping job for channel %lld",
                         queue_.size(), static_cast<long long>(j.channel_id));
                recordDeadLetter(j, "queue_full");
                continue;
            }
            queue_.push_back(std::move(j));
            ++enqueued;
        }
    }
    if (enqueued) q_cv_.notify_all();
}

void NotificationManager::workerLoop() {
    using namespace std::chrono;
    while (running_.load()) {
        DeliveryJob job;
        bool have = false;
        {
            std::unique_lock<std::mutex> lk(q_mu_);
            q_cv_.wait_for(lk, milliseconds(250), [&]{
                return !running_.load() || !queue_.empty();
            });
            if (!running_.load()) return;
            if (queue_.empty()) continue;

            // Pop the earliest job whose not_before has elapsed.
            auto now = steady_clock::now();
            auto it = std::find_if(queue_.begin(), queue_.end(),
                [now](const DeliveryJob& j){ return j.not_before <= now; });
            if (it == queue_.end()) continue;
            job = std::move(*it);
            queue_.erase(it);
            have = true;
        }
        if (!have) continue;

        bool ok = false;
        std::string err;
        try {
            ok = job.channel ? job.channel->send(job.event) : false;
            if (!ok) err = "channel.send returned false";
        } catch (const std::exception& e) {
            ok = false; err = e.what();
        } catch (...) {
            ok = false; err = "unknown exception";
        }

        auto& m = obs::Registry::instance().global();
        if (ok) {
            m.notifications_sent_total.inc();
            continue;
        }
        m.notifications_failed_total.inc();

        ++job.attempt;
        constexpr int kMaxAttempts = 5;
        if (job.attempt < kMaxAttempts) {
            // Exponential backoff with cap: 1s, 2s, 4s, 8s, 16s.
            int delay_s = 1 << std::min(job.attempt, 4);
            job.not_before = steady_clock::now() + seconds(delay_s);
            NVR_WARN("notify", "retry attempt %d in %ds (channel=%lld, reason=%s)",
                     job.attempt, delay_s,
                     static_cast<long long>(job.channel_id), err.c_str());
            std::lock_guard<std::mutex> lk(q_mu_);
            if (queue_.size() < max_queue_) {
                queue_.push_back(std::move(job));
                q_cv_.notify_one();
            } else {
                recordDeadLetter(job, "retry_queue_full:" + err);
            }
        } else {
            NVR_ERROR("notify", "delivery failed permanently after %d attempts: %s",
                      job.attempt, err.c_str());
            recordDeadLetter(job, err);
        }
    }
}

void NotificationManager::recordDeadLetter(const DeliveryJob& job, const std::string& last_error) {
    try {
        nlohmann::json p = {
            {"camera_id", job.event.camera_id},
            {"type",      job.event.type},
            {"severity",  job.event.severity},
        };
        std::lock_guard<std::recursive_mutex> lk(store_.db().mutex());
        SQLite::Statement q(store_.db().raw(),
            "INSERT INTO notify_dead_letter(channel_id, rule_id, attempts, last_error, payload_json) "
            "VALUES(?,?,?,?,?)");
        q.bind(1, job.channel_id);
        q.bind(2, job.rule_id);
        q.bind(3, job.attempt);
        q.bind(4, last_error);
        q.bind(5, p.dump());
        q.exec();
    } catch (const std::exception& e) {
        NVR_WARN("notify", "dead letter insert failed: %s", e.what());
    }
}

}
