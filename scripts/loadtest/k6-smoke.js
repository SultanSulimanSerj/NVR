// k6 smoke: API + HLS manifest (requires NVR_BASE_URL, NVR_USER, NVR_PASS env).
import http from "k6/http";
import { check, sleep } from "k6";

const base = __ENV.NVR_BASE_URL || "https://nvr.local";
const user = __ENV.NVR_USER || "admin";
const pass = __ENV.NVR_PASS || "";

export const options = {
  vus: 2,
  duration: "30s",
  thresholds: { http_req_failed: ["rate<0.5"] },
};

export default function () {
  const login = http.post(`${base}/api/v1/auth/login`, JSON.stringify({ login: user, password: pass }), {
    headers: { "Content-Type": "application/json" },
  });
  check(login, { "login 200": (r) => r.status === 200 });
  let token = "";
  try {
    token = login.json("token") || "";
  } catch (_) {}
  const h = { Authorization: `Bearer ${token}` };
  const cams = http.get(`${base}/api/v1/cameras`, { headers: h });
  check(cams, { "cams 200": (r) => r.status === 200 });
  const id = (() => {
    try {
      const arr = cams.json();
      return Array.isArray(arr) && arr.length ? arr[0].id : "";
    } catch (_) {
      return "";
    }
  })();
  if (id) {
    const m3u8 = http.get(`${base}/live/${id}/index.m3u8`, { headers: h });
    check(m3u8, { "hls 200 or 404": (r) => r.status === 200 || r.status === 404 });
  }
  sleep(1);
}
