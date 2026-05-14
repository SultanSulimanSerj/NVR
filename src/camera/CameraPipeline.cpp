#include "nvr/CameraPipeline.hpp"

#include "nvr/EventBus.hpp"
#include "nvr/Logger.hpp"
#include "nvr/PreEventBuffer.hpp"
#include "nvr/RecordingMuxerPolicy.hpp"
#include "nvr/RecordingSchedule.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#if NVR_WITH_OPENCV
#include <opencv2/imgproc.hpp>
#endif

namespace nvr {

namespace {

#if NVR_WITH_OPENCV
std::string snapshotPath(const std::string& dir,
                         const std::string& cam_id,
                         std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << dir << "/" << cam_id << "_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".jpg";
    return oss.str();
}
#endif

}

CameraPipeline::CameraPipeline(CameraConfig         cam,
                               ArchiveManager*      archive,
                               MotionConfig         motion_cfg,
                               bool                 hook_include_frame,
                               ThreadSafeQueue<MotionEvent>* motion_queue,
                               EventBus*            event_bus,
                               std::string          hls_root)
    : cam_(std::move(cam)),
      archive_(archive),
      motion_cfg_(std::move(motion_cfg)),
      hook_include_frame_(hook_include_frame),
      motion_queue_(motion_queue),
      event_bus_(event_bus),
      hls_root_(std::move(hls_root)) {}

CameraPipeline::~CameraPipeline() { stop(); }

void CameraPipeline::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&CameraPipeline::run, this);
}

void CameraPipeline::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

void CameraPipeline::run() {
    NVR_INFO("camera", "[%s] thread started (rtsp=%s, hw=%s)",
             cam_.id.c_str(), cam_.rtsp_url.c_str(),
             hwAccelToString(cam_.preferred_hw));

    auto& cam_metrics = obs::Registry::instance().camera(cam_.id);
    cam_metrics.state.set(0.0);

    int backoff_seconds = 1;
    while (running_.load()) {
        try {
            cam_metrics.state.set(1.0);
            runOnce();
            cam_metrics.state.set(0.0);
            backoff_seconds = 1;
        } catch (const std::exception& e) {
            NVR_ERROR("camera", "[%s] runOnce exception: %s", cam_.id.c_str(), e.what());
            cam_metrics.decoder_errors_total.inc();
            cam_metrics.state.set(-1.0);
        }
        if (!running_.load()) break;

        cam_metrics.rtsp_reconnects_total.inc();
        NVR_WARN("camera", "[%s] reconnect in %d s", cam_.id.c_str(), backoff_seconds);
        for (int i = 0; i < backoff_seconds && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        backoff_seconds = std::min(backoff_seconds * 2, 30);
    }

    NVR_INFO("camera", "[%s] thread stopped", cam_.id.c_str());
}

void CameraPipeline::runOnce() {
    FFmpegDecoder decoder;
    if (!decoder.open(cam_.rtsp_url, cam_.preferred_hw)) {
        return;
    }

    AVStream* vs = decoder.inputCtx()->streams[decoder.videoStreamIndex()];

    SegmentWriter writer;
    auto segment_started_at = std::chrono::system_clock::now();
    bool segment_has_motion = false;

#if NVR_WITH_OPENCV
    std::unique_ptr<MotionDetector> motion;
    if (cam_.enable_motion) {
        motion = std::make_unique<MotionDetector>(motion_cfg_);
        motion->setRois(cam_.motion_rois);
    }
    cv::Mat         bgr_full;
    int             sws_w = 0, sws_h = 0;
    AVPixelFormat   sws_src_fmt = AV_PIX_FMT_NONE;
    SwsContext*     sws = nullptr;
    const bool      motion_enabled = cam_.enable_motion;
#else
    const bool      motion_enabled = false;
#endif

    const bool motion_record =
        motionRecordingActive(cam_.enable_recording, cam_.recording_mode, motion_enabled);
    if (cam_.enable_recording && cam_.recording_mode == RecordingMode::Motion && !motion_record) {
        NVR_WARN("camera", "[%s] recording_mode=motion requires enable_motion and OpenCV build; "
                          "falling back to continuous recording",
                 cam_.id.c_str());
    }
    if (cam_.enable_recording && cam_.recording_mode == RecordingMode::Hybrid && !motion_enabled) {
        NVR_WARN("camera", "[%s] recording_mode=hybrid without motion detector; "
                          "recording continues but segments will not be motion-marked",
                 cam_.id.c_str());
    }

    bool need_open_segment =
        cam_.enable_recording && !motion_record &&
        recordingScheduleAllowsLocalNow(cam_.recording_schedule_json);
    std::chrono::steady_clock::time_point motion_latch_until{};
    bool motion_need_keyframe = motion_record;

    FramePtr hw_frame(av_frame_alloc());
    FramePtr sw_frame(av_frame_alloc());
    if (!hw_frame || !sw_frame) {
        NVR_ERROR("camera", "[%s] av_frame_alloc failed", cam_.id.c_str());
        return;
    }

    HlsMuxer         hls;
    SubStreamEncoder sub_enc;
    const bool       want_substream = cam_.enable_substream && !hls_root_.empty();
    bool             sub_initialized = false;

    std::unique_ptr<PreEventBuffer> pre_buf;
    if (cam_.enable_recording && cam_.pre_event_seconds > 0)
        pre_buf = std::make_unique<PreEventBuffer>(std::chrono::seconds(cam_.pre_event_seconds));

    const int  analysis_period_ms =
        (cam_.analysis_fps > 0) ? (1000 / cam_.analysis_fps) : 0;
    auto last_analysis_at = std::chrono::steady_clock::time_point::min();

    auto finalize_segment = [&]() {
        if (!writer.isOpen()) return;
        auto path = writer.path();
        auto sz   = writer.bytesWritten();
        writer.close();
        // Only publish to retention / DB after the .tmp was renamed atomically.
        if (!writer.wasFinalized()) {
            NVR_WARN("camera", "[%s] segment %s NOT finalized; skipping DB insert",
                     cam_.id.c_str(), path.c_str());
            return;
        }
        SegmentInfo si;
        si.camera_id  = cam_.id;
        si.path       = path;
        si.started_at = segment_started_at;
        si.ended_at   = std::chrono::system_clock::now();
        si.size_bytes = static_cast<int64_t>(sz);
        si.has_motion = segment_has_motion;
        if (archive_) archive_->registerSegment(si);
    };

    // GOP-aligned rotation: when the wall-clock window is up we wait for the
    // *next* keyframe and rotate at that boundary, so the new segment can stand
    // on its own from frame zero. `pending_rotate` tracks that intent.
    bool pending_rotate = false;

    auto open_segment_if_needed = [&](bool force = false) {
        if (!cam_.enable_recording) return true;
        if (writer.isOpen() && !force &&
            !archive_->shouldRotateSegment(segment_started_at)) return true;
        if (writer.isOpen() && !force) {
            pending_rotate = true;
            return true;
        }

        // ENOSPC pre-check: don't even try to open a segment when the FS is
        // already past the configured release threshold.
        if (archive_ && archive_->currentUsageRatio() > 0.99) {
            NVR_WARN("camera", "[%s] disk near-full, deferring new segment", cam_.id.c_str());
            return false;
        }

        if (writer.isOpen()) finalize_segment();
        segment_started_at  = std::chrono::system_clock::now();
        segment_has_motion  = false;
        auto path = archive_->nextSegmentPath(cam_.id, segment_started_at);
        bool ok = writer.open(path, decoder.codecCtx(), vs, decoder.activeHwAccel());
        if (ok) {
            pending_rotate = false;
            if (pre_buf) {
                auto refs = pre_buf->snapshotRefs();
                for (auto* rp : refs) {
                    writer.writePacket(rp, vs->time_base);
                    av_packet_free(&rp);
                }
            }
        }
        return ok;
    };

    if (need_open_segment) {
        if (!open_segment_if_needed()) {
            NVR_ERROR("camera", "[%s] cannot open initial segment", cam_.id.c_str());
        }
    }

    while (running_.load()) {
        int rd_err = 0;
        PacketPtr pkt = decoder.readPacket(rd_err);
        if (rd_err == AVERROR_EOF) {
            NVR_WARN("camera", "[%s] EOF from RTSP", cam_.id.c_str());
            break;
        }
        if (rd_err == AVERROR(EAGAIN)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (rd_err < 0) {
            NVR_WARN("camera", "[%s] av_read_frame error", cam_.id.c_str());
            break;
        }
        if (pkt->stream_index != decoder.videoStreamIndex()) continue;

        auto& cam_metrics_loop = obs::Registry::instance().camera(cam_.id);
        cam_metrics_loop.bytes_in_total.inc(static_cast<uint64_t>(pkt->size));

        if (cam_.enable_recording) {
            const bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            const bool latch_ok = motionLatchAllowsWrite(
                motion_record, std::chrono::steady_clock::now(), motion_latch_until);
            const bool schedule_ok =
                recordingScheduleAllowsLocalNow(cam_.recording_schedule_json);
            const bool mux_allowed = latch_ok && schedule_ok;

            if (shouldFinalizeRecordingOnKeyframe(mux_allowed, writer.isOpen(), is_key)) {
                finalize_segment();
                motion_need_keyframe = true;
            }

            if (mayWriteRecordingPacket(motion_record, latch_ok, schedule_ok)) {
                const bool skip_open_write =
                    skipWriteUntilNextKeyframe(motion_record, motion_need_keyframe, is_key);
                if (!skip_open_write) {
                    open_segment_if_needed();
                    if (pending_rotate && is_key) {
                        open_segment_if_needed(/*force=*/true);
                    }
                    if (writer.isOpen() &&
                        writer.writePacket(pkt.get(), vs->time_base)) {
                        cam_metrics_loop.packets_written_total.inc();
                        cam_metrics_loop.bytes_recorded_total.inc(
                            static_cast<uint64_t>(pkt->size));
                    }
                    if (motion_record && motion_need_keyframe && writer.isOpen() && is_key) {
                        motion_need_keyframe = false;
                    }
                }
            }

            if (pre_buf) pre_buf->push(pkt.get());
        }

        if (!motion_enabled) continue;

        if (!decoder.sendPacket(pkt.get())) continue;

        while (true) {
            av_frame_unref(hw_frame.get());
            int err = decoder.receiveFrame(hw_frame.get());
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) break;
            if (err < 0) {
                NVR_WARN("camera", "[%s] receiveFrame error", cam_.id.c_str());
                break;
            }

            auto now = std::chrono::steady_clock::now();
            if (analysis_period_ms > 0 &&
                now - last_analysis_at < std::chrono::milliseconds(analysis_period_ms)) {
                continue;
            }
            last_analysis_at = now;

            av_frame_unref(sw_frame.get());
            if (!decoder.downloadIfHardware(hw_frame.get(), sw_frame.get())) continue;

            if (want_substream && !sub_initialized) {
                sub_initialized = true;
                auto camera_hls_dir = std::filesystem::path(hls_root_) / cam_.id;
                if (sub_enc.open(cam_, decoder.codecCtx(), &hls)) {
                    if (!hls.open(camera_hls_dir, sub_enc.codecContext())) {
                        NVR_WARN("camera", "[%s] HLS muxer failed to open", cam_.id.c_str());
                        sub_enc.close();
                    } else {
                        NVR_INFO("camera", "[%s] sub-stream HLS ready: %s",
                                 cam_.id.c_str(), camera_hls_dir.c_str());
                    }
                }
            }
            if (sub_enc.isOpen()) {
                sub_enc.encode(sw_frame.get());
            }

#if NVR_WITH_OPENCV
            auto src_fmt = static_cast<AVPixelFormat>(sw_frame->format);
            if (sws_src_fmt != src_fmt || sws_w != sw_frame->width || sws_h != sw_frame->height) {
                if (sws) { sws_freeContext(sws); sws = nullptr; }
                sws = sws_getContext(sw_frame->width, sw_frame->height, src_fmt,
                                     sw_frame->width, sw_frame->height, AV_PIX_FMT_BGR24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
                sws_w = sw_frame->width; sws_h = sw_frame->height; sws_src_fmt = src_fmt;
                bgr_full.create(sws_h, sws_w, CV_8UC3);
            }
            if (!sws) continue;

            uint8_t* dst[1]     = {bgr_full.data};
            int      dst_lin[1] = {static_cast<int>(bgr_full.step[0])};
            sws_scale(sws, sw_frame->data, sw_frame->linesize, 0, sws_h, dst, dst_lin);

            if (motion) {
                auto r = motion->process(bgr_full);
                cam_metrics_loop.frames_received_total.inc();
                if (r.motion) {
                    segment_has_motion = true;
                    motion_latch_until =
                        std::chrono::steady_clock::now() +
                        std::chrono::seconds(std::max(1, cam_.post_event_seconds));
                }
                if (r.motion && motion->shouldEmit() && motion_queue_) {
                    cam_metrics_loop.motion_events_total.inc();
                    MotionEvent ev;
                    ev.camera_id     = cam_.id;
                    ev.timestamp     = std::chrono::system_clock::now();
                    ev.area_ratio    = r.area_ratio;
                    ev.snapshot_path = snapshotPath(motion_cfg_.snapshot_dir, cam_.id,
                                                    ev.timestamp);
                    motion->saveSnapshot(bgr_full, ev.snapshot_path);

                    if (hook_include_frame_) {
                        ev.include_frame = true;
                        ev.width    = bgr_full.cols;
                        ev.height   = bgr_full.rows;
                        ev.channels = 3;
                        ev.bgr_pixels.assign(bgr_full.datastart, bgr_full.dataend);
                    }

                    NVR_INFO("motion", "[%s] motion area=%.4f -> %s",
                             cam_.id.c_str(), r.area_ratio, ev.snapshot_path.c_str());
                    if (event_bus_) {
                        SystemEvent sev;
                        sev.camera_id     = cam_.id;
                        sev.type          = "motion.detected";
                        sev.severity       = "info";
                        sev.snapshot_path = ev.snapshot_path;
                        nlohmann::json jp{
                            {"area_ratio", r.area_ratio},
                            {"snapshot_path", ev.snapshot_path},
                        };
                        sev.payload_json = jp.dump();
                        event_bus_->publish(std::move(sev));
                    }
                    motion_queue_->tryPush(std::move(ev));
                }
            }
#endif
        }
    }

#if NVR_WITH_OPENCV
    if (sws) sws_freeContext(sws);
#endif

    if (sub_enc.isOpen()) sub_enc.flush();
    sub_enc.close();
    hls.close();

    finalize_segment();
}

}
