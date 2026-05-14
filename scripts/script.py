"""
Хук, вызываемый ядром NVR при детекции движения.

Сигнатура:
    on_motion(camera_id: str,
              timestamp_iso: str,
              snapshot_path: str,
              area_ratio: float,
              frame: numpy.ndarray | None) -> None

`frame` передаётся только при `python.include_frame: true` в конфиге.
Изображение приходит в формате BGR (H, W, 3), uint8.

Логика по умолчанию: запись в лог и опциональные расширения
(MQTT/HTTP/Telegram) — заглушки, заполните под свою инфраструктуру.
"""
from __future__ import annotations

import json
import logging
import os
import sys
from pathlib import Path

LOG_PATH = Path(os.environ.get("NVR_HOOK_LOG", "/var/log/nvr-prototype/hooks.log"))
LOG_PATH.parent.mkdir(parents=True, exist_ok=True)

logging.basicConfig(
    filename=str(LOG_PATH),
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("nvr.hook")


def on_motion(camera_id, timestamp_iso, snapshot_path, area_ratio,
              frame=None, clip_path=None, roi_name=None):
    event = {
        "camera_id": camera_id,
        "timestamp": timestamp_iso,
        "snapshot":  snapshot_path,
        "clip":      clip_path,
        "roi":       roi_name,
        "area_ratio": round(float(area_ratio), 5),
        "has_frame": frame is not None,
    }
    if frame is not None:
        event["frame_shape"] = list(frame.shape)
        event["frame_dtype"] = str(frame.dtype)
    log.info(json.dumps(event, ensure_ascii=False))


if __name__ == "__main__":
    on_motion("test-cam", "2026-01-01T00:00:00", "/tmp/x.jpg", 0.123, None,
              clip_path="/tmp/clip.mp4", roi_name="zone-1")
    print("ok", file=sys.stderr)
