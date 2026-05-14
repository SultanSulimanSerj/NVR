#pragma once

#include "nvr/EventBus.hpp"
#include "nvr/notify/Channels.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nvr::notify {

struct ChannelEntry {
    int64_t                                  id{};
    std::string                              kind;
    std::string                              name;
    std::shared_ptr<INotificationChannel>    channel;  // shared so a reload() can swap underneath in-flight workers
};

struct Rule {
    int64_t      id{};
    std::string  camera_id;
    std::string  event_type;
    std::string  severity_min{"info"};
    int          throttle_seconds{30};
    int64_t      channel_id{};
};

struct DeliveryJob {
    int64_t                                channel_id{};
    int64_t                                rule_id{};
    std::shared_ptr<INotificationChannel>  channel;
    SystemEvent                            event;
    int                                    attempt{0};
    std::chrono::steady_clock::time_point  not_before;
};

class NotificationManager {
public:
    NotificationManager(store::ConfigStore& store, EventBus& bus,
                         std::size_t worker_count = 4, std::size_t max_queue = 1024);
    ~NotificationManager();

    void start();
    void stop();
    void reload();

    // For tests / health: snapshot of queue depth.
    std::size_t queueDepth();

private:
    void dispatch(const SystemEvent& ev);
    void workerLoop();
    void recordDeadLetter(const DeliveryJob& job, const std::string& last_error);
    int  severityRank(const std::string& s) const;

    store::ConfigStore&                       store_;
    EventBus&                                 bus_;
    uint64_t                                  sub_id_{0};

    std::mutex                                cfg_mu_;
    std::unordered_map<int64_t, ChannelEntry> channels_;
    std::vector<Rule>                         rules_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_sent_;

    // Worker pool / bounded job queue.
    std::mutex                                q_mu_;
    std::condition_variable                   q_cv_;
    std::deque<DeliveryJob>                   queue_;
    std::vector<std::thread>                  workers_;
    std::atomic<bool>                         running_{false};
    std::size_t                               worker_count_;
    std::size_t                               max_queue_;
};

}
