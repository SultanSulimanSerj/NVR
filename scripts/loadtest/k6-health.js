// k6: только публичный /api/v1/health (для CI без учётных данных).
import http from "k6/http";
import { check, sleep } from "k6";

const base = (__ENV.NVR_BASE_URL || "https://nvr.local").replace(/\/$/, "");

export const options = {
  vus: 2,
  duration: "20s",
  thresholds: { http_req_failed: ["rate<0.1"] },
};

export default function () {
  const r = http.get(`${base}/api/v1/health`);
  check(r, { "health 200": (x) => x.status === 200 });
  sleep(0.5);
}
