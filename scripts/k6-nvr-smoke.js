import http from "k6/http";
import { check, sleep } from "k6";

export const options = {
  vus: 5,
  duration: "30s",
  thresholds: {
    http_req_failed: ["rate<0.01"],
    http_req_duration: ["p(95)<500"],
  },
};

export default function () {
  const r1 = http.get(`${__ENV.BASE_URL || "http://127.0.0.1:8080"}/healthz`);
  check(r1, { "healthz 200": (r) => r.status === 200 });
  const r2 = http.get(`${__ENV.BASE_URL || "http://127.0.0.1:8080"}/api/v1/health`);
  check(r2, { "api health 200": (r) => r.status === 200 });
  sleep(0.3);
}
