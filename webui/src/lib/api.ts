import { create } from "zustand";

interface AuthState {
  token: string | null;
  role:  string | null;
  login: string | null;
  setToken: (t: string | null, role?: string | null, login?: string | null) => void;
}

// Access token lives in memory only. Refresh token is stored in an HttpOnly
// cookie by the backend (`Set-Cookie: nvr_refresh=…; HttpOnly`) which JS cannot
// read or steal. We still persist the *role* + *login* to localStorage so the
// router can paint protected shells before the first /me round-trip.
let _accessToken: string | null = sessionStorage.getItem("nvr.access") || null;

export const useAuth = create<AuthState>((set) => ({
  token: _accessToken,
  role:  localStorage.getItem("nvr.role"),
  login: localStorage.getItem("nvr.login"),
  setToken: (t, role = null, login = null) => {
    _accessToken = t;
    if (t) {
      sessionStorage.setItem("nvr.access", t);
      if (role)  localStorage.setItem("nvr.role", role);
      if (login) localStorage.setItem("nvr.login", login);
    } else {
      sessionStorage.removeItem("nvr.access");
      localStorage.removeItem("nvr.role");
      localStorage.removeItem("nvr.login");
    }
    set({ token: t, role, login });
  },
}));

const baseHeaders = () => {
  const h: Record<string, string> = { "Content-Type": "application/json" };
  if (_accessToken) h["Authorization"] = `Bearer ${_accessToken}`;
  return h;
};

let _refreshing: Promise<boolean> | null = null;

async function tryRefresh(): Promise<boolean> {
  if (_refreshing) return _refreshing;
  _refreshing = (async () => {
    try {
      const res = await fetch("/api/v1/auth/refresh", {
        method: "POST",
        credentials: "include",
        headers: { "Content-Type": "application/json" },
      });
      if (!res.ok) return false;
      const body = await res.json();
      _accessToken = body.access_token || body.token || null;
      if (_accessToken) {
        sessionStorage.setItem("nvr.access", _accessToken);
        useAuth.setState({ token: _accessToken });
        return true;
      }
      return false;
    } catch {
      return false;
    } finally {
      _refreshing = null;
    }
  })();
  return _refreshing;
}

async function rawFetch(path: string, init: RequestInit): Promise<Response> {
  return fetch(path, {
    ...init,
    credentials: "include",
    headers: { ...baseHeaders(), ...(init.headers || {}) },
  });
}

/** Structured API failure (4xx/5xx with optional JSON `{ error, ... }`). */
export class NvrApiError extends Error {
  readonly status: number;
  readonly code?: string;
  readonly details?: Record<string, unknown>;
  readonly bodyText?: string;

  constructor(
    status: number,
    message: string,
    opts?: { code?: string; details?: Record<string, unknown>; bodyText?: string },
  ) {
    super(message);
    this.status = status;
    this.code = opts?.code;
    this.details = opts?.details;
    this.bodyText = opts?.bodyText;
    this.name = "NvrApiError";
    Object.setPrototypeOf(this, NvrApiError.prototype);
  }
}

/** User-facing string for toasts; pass `t` from useTranslation for localized codes. */
export function toastMessageForApiError(
  e: unknown,
  t: (key: string, opt?: Record<string, string>) => string,
): string {
  if (e instanceof NvrApiError && e.code === "license_channel_limit") {
    return t("errors.license_channel_limit", {
      max: String(e.details?.max_channels ?? "?"),
      configured: String(e.details?.configured ?? "?"),
    });
  }
  if (e instanceof NvrApiError && e.code === "motion_recording_requires_enable_motion") {
    return t("errors.motion_recording_requires_enable_motion");
  }
  if (e instanceof NvrApiError && e.code === "hybrid_requires_enable_motion") {
    return t("errors.hybrid_requires_enable_motion");
  }
  if (e instanceof NvrApiError && e.code === "license_mod_required") {
    return t("errors.license_mod_required");
  }
  if (e instanceof Error) return e.message;
  return String(e);
}

function buildHttpErrorMessage(status: number, text: string, code?: string, details?: Record<string, unknown>): string {
  if (code === "license_channel_limit" && details) {
    const mx = details.max_channels;
    const cf = details.configured;
    return `Camera limit reached (max ${mx}, configured ${cf})`;
  }
  if (code) return `HTTP ${status}: ${code}`;
  const snippet = text.replace(/\s+/g, " ").trim().slice(0, 400);
  return snippet ? `HTTP ${status}: ${snippet}` : `HTTP ${status}`;
}

export async function api<T>(path: string, init: RequestInit = {}): Promise<T> {
  let res = await rawFetch(path, init);
  if (res.status === 401) {
    const ok = await tryRefresh();
    if (ok) {
      res = await rawFetch(path, init);
    }
    if (res.status === 401) {
      useAuth.getState().setToken(null);
      window.dispatchEvent(new CustomEvent("nvr:unauthorized"));
      throw new Error("unauthorized");
    }
  }
  if (!res.ok) {
    const text = await res.text();
    let code: string | undefined;
    let details: Record<string, unknown> | undefined;
    try {
      const j = JSON.parse(text) as Record<string, unknown>;
      if (typeof j.error === "string") code = j.error;
      details = j;
    } catch {
      /* plain text body */
    }
    const msg = buildHttpErrorMessage(res.status, text, code, details);
    throw new NvrApiError(res.status, msg, { code, details, bodyText: text });
  }
  if (res.status === 204) return undefined as T;
  const ct = res.headers.get("content-type") || "";
  return ct.includes("json") ? res.json() : (await res.text() as unknown as T);
}

/** Same auth/401 refresh as `api`, but returns the response body as a Blob (for binary endpoints). */
export async function apiBlob(path: string, init: RequestInit = {}): Promise<Blob> {
  let res = await rawFetch(path, init);
  if (res.status === 401) {
    const ok = await tryRefresh();
    if (ok) {
      res = await rawFetch(path, init);
    }
    if (res.status === 401) {
      useAuth.getState().setToken(null);
      window.dispatchEvent(new CustomEvent("nvr:unauthorized"));
      throw new Error("unauthorized");
    }
  }
  if (!res.ok) {
    const text = await res.text();
    let code: string | undefined;
    let details: Record<string, unknown> | undefined;
    try {
      const j = JSON.parse(text) as Record<string, unknown>;
      if (typeof j.error === "string") code = j.error;
      details = j;
    } catch {
      /* plain text body */
    }
    const msg = buildHttpErrorMessage(res.status, text, code, details);
    throw new NvrApiError(res.status, msg, { code, details, bodyText: text });
  }
  return res.blob();
}

// -----------------------------------------------------------------------------
export type Role = "admin" | "operator" | "viewer";

export type RecordingMode = "continuous" | "motion" | "hybrid";

export interface Camera {
  id: string;
  name: string;
  rtsp_url: string;
  sub_rtsp_url?: string;
  preferred_hw: string;
  analysis_fps: number;
  enable_motion: boolean;
  enable_recording: boolean;
  recording_mode?: RecordingMode;
  enable_substream: boolean;
  onvif_host?: string;
  onvif_port?: number;
  onvif_user?: string;
  onvif_pass?: string;
  pre_event_seconds: number;
  post_event_seconds: number;
  sub_bitrate_kbps: number;
  sub_width: number;
  sub_height: number;
  sub_fps: number;
  motion_rois?: Array<{ name: string; polygon: Array<[number, number]> }>;
  recording_schedule?: {
    always: boolean;
    weekdays?: Record<string, Array<{ start: string; end: string }>>;
  };
  /** 0..1 on facility map; negative when unset */
  plan_x?: number;
  plan_y?: number;
}

export interface NvrEvent {
  id: number;
  camera_id: string;
  ts: string;
  type: string;
  severity: string;
  payload: unknown;
  snapshot_path?: string;
  clip_path?: string;
  acknowledged: boolean;
}

export interface AiDetectionRow {
  id: number;
  camera_id: string;
  ts: string;
  type: string;
  confidence: number;
  track_id: number;
  bbox: unknown;
}

export interface Segment {
  id: number;
  camera_id: string;
  path: string;
  started_at: string;
  ended_at: string;
  duration_ms: number;
  size_bytes: number;
  has_motion: boolean;
}

export interface ArchiveTimelineBucket {
  bucket_start: string;
  bucket_start_unix: number;
  segment_count: number;
  has_motion: boolean;
  duration_ms_total: number;
  event_count?: number;
}

export interface ArchiveTimelineResponse {
  bucket_seconds: number;
  bucket_minutes: number;
  camera_id: string;
  from: string;
  to: string;
  buckets: ArchiveTimelineBucket[];
}

export interface CameraStreamStats {
  camera_id: string;
  fps: number;
  bitrate_kbps: number;
  pipeline_lag_ms: number;
  state: number;
  frames_received_total: number;
  frames_dropped_total: number;
  decoder_errors_total: number;
  rtsp_reconnects_total: number;
  bytes_in_total: number;
  bytes_recorded_total: number;
  inference_ms: number;
  inference_fps: number;
}

export interface User { id: number; login: string; role: Role; disabled: boolean; has_totp: boolean; }
export interface Channel { id: number; kind: string; name: string; enabled: boolean; config?: unknown; }
export interface NotifyDlqRow {
  id: number;
  ts: string;
  channel_id: number | null;
  rule_id: number | null;
  attempts: number;
  last_error: string;
  payload_json: unknown;
}
export interface Rule {
  id: number; camera_id?: string; event_type: string;
  severity_min: string; throttle_seconds: number; channel_id: number;
}
export interface AuditEntry { id: number; ts: string; user: string; action: string; target: string; payload: string; }

export type LicenseMode = "trial" | "licensed" | "degraded";

export interface LicenseStatus {
  mode: LicenseMode;
  max_channels: number;
  channels_configured: number;
  modules_bitmask: number;
  signature_ok: boolean;
  license_file_present: boolean;
  attempted_verify: boolean;
  fingerprint_preview: string;
  expires_at: string | null;
}

export interface SystemInfo {
  version: string; time: string; uptime: string; hostname: string;
  cpu_model: string; memory_kb_total: number; cpu_temps_c: number[]; cameras_active: number;
  features?: { mqtt?: boolean; mqtt_delivery?: string; license?: LicenseStatus };
}
export interface MotionConfig {
  downscale_width: number; downscale_height: number;
  history: number; var_threshold: number; detect_shadows: boolean;
  min_area_ratio: number; cooldown_seconds: number;
  snapshot_dir: string; snapshot_jpeg_quality: number;
}
export interface ArchiveConfig {
  root_path: string; segment_minutes: number;
  target_usage_ratio: number; release_to_ratio: number; min_keep_minutes: number;
  file_prefix?: string; file_extension?: string;
  extra_archive_roots?: string[];
  export_watermark_text?: string;
}

export interface ArchiveRootHealth {
  path: string;
  exists: boolean;
  writable: boolean;
  free_bytes: number;
  capacity_bytes: number;
}

/** Optional JSON array from /etc/nvr-prototype/camera_catalog.json (see deploy/camera_catalog.sample.json). */
export interface CameraCatalogEntry {
  vendor?: string;
  model?: string;
  firmware_notes?: string;
  onvif_events?: string;
  rtsp_notes?: string;
  [key: string]: unknown;
}

export interface CameraCatalogResponse {
  items: CameraCatalogEntry[];
  source: string;
  loaded: boolean;
  error?: string;
}

// -----------------------------------------------------------------------------
export const Cameras = {
  list:   () => api<Camera[]>("/api/v1/cameras"),
  get:    (id: string) => api<Camera>(`/api/v1/cameras/${id}`),
  create: (c: Partial<Camera>) => api<Camera>("/api/v1/cameras", { method: "POST", body: JSON.stringify(c) }),
  update: (id: string, c: Partial<Camera>) =>
    api<Camera>(`/api/v1/cameras/${id}`, { method: "PATCH", body: JSON.stringify(c) }),
  remove: (id: string) => api<void>(`/api/v1/cameras/${id}`, { method: "DELETE" }),
  testRtsp: (rtsp_url: string) =>
    api<{ reachable: boolean; host: string; port: number }>(
      "/api/v1/cameras/test-rtsp", { method: "POST", body: JSON.stringify({ rtsp_url }) }),
  streamStats: (id: string) => api<CameraStreamStats>(`/api/v1/cameras/${encodeURIComponent(id)}/stream-stats`),
};

interface LoginResponse {
  token: string;
  access_token?: string;
  refresh_token?: string;
  expires_in?: number;
  role: string;
  login: string;
}

export const Auth = {
  login: (login: string, password: string, totp_code?: string) =>
    api<LoginResponse>("/api/v1/auth/login",
      { method: "POST", body: JSON.stringify({ login, password, totp_code }) }),
  me: () => api<{ login: string; role: Role }>("/api/v1/auth/me"),
  logout: () => api<{ ok: boolean }>("/api/v1/auth/logout", { method: "POST" })
                .catch(() => ({ ok: false })),
  changePassword: (old_password: string, new_password: string) =>
    api<{ ok: boolean }>("/api/v1/auth/change-password",
      { method: "POST", body: JSON.stringify({ old_password, new_password }) }),
  totpSetup: () =>
    api<{ secret: string; otpauth_url: string }>("/api/v1/auth/totp/setup", { method: "POST" }),
  totpVerify: (code: string) =>
    api<{ ok: boolean }>("/api/v1/auth/totp/verify",
      { method: "POST", body: JSON.stringify({ code }) }),
  totpDisable: () => api<{ ok: boolean }>("/api/v1/auth/totp/disable", { method: "POST" }),
};

export const Events = {
  list: (q: URLSearchParams = new URLSearchParams()) => api<NvrEvent[]>(`/api/v1/events?${q.toString()}`),
  searchDetections: (q: URLSearchParams) =>
    api<AiDetectionRow[]>(`/api/v1/events/search?${q.toString()}`),
  ack:  (id: number) => api<{ ok: boolean }>(`/api/v1/events/${id}/ack`, { method: "POST" }),
  ackAll: () => api<{ updated: number }>("/api/v1/events/ack-all", { method: "POST" }),
};

export const Archive = {
  segments: (q: URLSearchParams = new URLSearchParams()) =>
    api<Segment[]>(`/api/v1/archive/segments?${q.toString()}`),
  timeline: (q: URLSearchParams) =>
    api<ArchiveTimelineResponse>(`/api/v1/archive/timeline?${q.toString()}`),
  rootsHealth: () => api<{ roots: ArchiveRootHealth[] }>("/api/v1/archive/roots-health"),
  playUrl:  (id: number) => `/api/v1/archive/segments/${id}/play.mp4`,
  exportUrl: (id: number) => `/api/v1/archive/segments/${id}/export.mp4`,
  exportRangeBlob: (
    camera_id: string,
    fromIso: string,
    toIso: string,
    opts?: { watermark_text?: string }
  ) =>
    apiBlob("/api/v1/archive/export", {
      method: "POST",
      body: JSON.stringify(
        opts?.watermark_text !== undefined
          ? { camera_id, from: fromIso, to: toIso, watermark_text: opts.watermark_text }
          : { camera_id, from: fromIso, to: toIso }
      ),
    }),
};

export const Users = {
  list:   () => api<User[]>("/api/v1/users"),
  create: (login: string, password: string, role: Role) =>
    api<void>("/api/v1/users", { method: "POST", body: JSON.stringify({ login, password, role }) }),
  patch:  (login: string, body: { password?: string; disabled?: boolean }) =>
    api<void>(`/api/v1/users/${encodeURIComponent(login)}`, { method: "PATCH", body: JSON.stringify(body) }),
  remove: (login: string) => api<void>(`/api/v1/users/${encodeURIComponent(login)}`, { method: "DELETE" }),
  camerasGet: (login: string) =>
    api<{ login: string; camera_ids: string[] }>(`/api/v1/users/${encodeURIComponent(login)}/cameras`),
  camerasPut: (login: string, camera_ids: string[]) =>
    api(`/api/v1/users/${encodeURIComponent(login)}/cameras`,
      { method: "PUT", body: JSON.stringify({ camera_ids }) }),
};

export const Config = {
  motionGet:  () => api<MotionConfig>("/api/v1/config/motion"),
  motionPut:  (m: MotionConfig) => api("/api/v1/config/motion",  { method: "PUT", body: JSON.stringify(m) }),
  archiveGet: () => api<ArchiveConfig>("/api/v1/config/archive"),
  archivePut: (a: ArchiveConfig) => api("/api/v1/config/archive", { method: "PUT", body: JSON.stringify(a) }),
  featuresGet: () => api<{ mqtt_delivery: string }>("/api/v1/config/features"),
  featuresPut: (mqtt_delivery: "stub" | "live") =>
    api("/api/v1/config/features", { method: "PUT", body: JSON.stringify({ mqtt_delivery }) }),
  mapGet: () => api<{ plan_image_url: string }>("/api/v1/config/map"),
  mapPut: (plan_image_url: string) =>
    api("/api/v1/config/map", { method: "PUT", body: JSON.stringify({ plan_image_url }) }),
};

export const Notifications = {
  channels:       () => api<Channel[]>("/api/v1/notifications/channels"),
  channel:        (id: number) => api<Channel>(`/api/v1/notifications/channels/${id}`),
  createChannel:  (kind: string, name: string, config: unknown) =>
    api<{ id: number }>("/api/v1/notifications/channels",
      { method: "POST", body: JSON.stringify({ kind, name, config, enabled: true }) }),
  patchChannel:   (id: number, body: { name?: string; enabled?: boolean; config?: unknown }) =>
    api(`/api/v1/notifications/channels/${id}`, { method: "PATCH", body: JSON.stringify(body) }),
  removeChannel:  (id: number) => api(`/api/v1/notifications/channels/${id}`, { method: "DELETE" }),
  testChannel:    (id: number) => api(`/api/v1/notifications/channels/${id}/test`, { method: "POST" }),
  rules:          () => api<Rule[]>("/api/v1/notifications/rules"),
  createRule:     (r: Partial<Rule>) =>
    api<{ id: number }>("/api/v1/notifications/rules", { method: "POST", body: JSON.stringify(r) }),
  removeRule:     (id: number) => api(`/api/v1/notifications/rules/${id}`, { method: "DELETE" }),
  deadLetter:     (limit = 200) =>
    api<NotifyDlqRow[]>(`/api/v1/notifications/dead-letter?limit=${limit}`),
};

export const License = {
  get: () => api<LicenseStatus>("/api/v1/license"),
};

export const System = {
  info:     () => api<SystemInfo>("/api/v1/system/info"),
  network:  () => api<unknown>("/api/v1/system/network"),
  setNetwork: (netplan_yaml: string) =>
    api("/api/v1/system/network", { method: "PUT", body: JSON.stringify({ netplan_yaml }) }),
  time:     () => api<{ raw: string }>("/api/v1/system/time"),
  setTime:  (timezone: string) =>
    api("/api/v1/system/time", { method: "PUT", body: JSON.stringify({ timezone }) }),
  service:  (name: string, action: "start" | "stop" | "restart" | "status") =>
    api<{ output: string }>(`/api/v1/system/services/${name}/${action}`, { method: "POST" }),
  logs:     (unit = "nvr-prototype", lines = 200) =>
    api<{ log: string }>(`/api/v1/system/logs?unit=${unit}&lines=${lines}`),
  reboot:   () => api("/api/v1/system/power/reboot",   { method: "POST" }),
  shutdown: () => api("/api/v1/system/power/shutdown", { method: "POST" }),
  fieldBundle: () =>
    api<{
      time: string;
      telemetry_opt_in: boolean;
      archive_roots: ArchiveRootHealth[];
      active_cameras: number;
      configured_cameras: number;
    }>("/api/v1/system/field-bundle"),
  cameraCatalog: () => api<CameraCatalogResponse>("/api/v1/system/camera-catalog"),
};

export const Storage = {
  disks:   () => api<unknown>("/api/v1/storage/disks"),
  smart:   (dev: string) => api<unknown>(`/api/v1/storage/disks/${dev}/smart`),
  usage:   () => api<{ df: string; archive_used_ratio: number }>("/api/v1/storage/usage"),
};

export const Audit = {
  list: (q: URLSearchParams = new URLSearchParams()) =>
    api<AuditEntry[]>(`/api/v1/audit?${q.toString()}`),
};

export interface LiveLayoutRow {
  id: number;
  name: string;
  payload: { grid?: number; cols?: number };
  updated_at: string;
}

export const LiveLayouts = {
  list: () => api<LiveLayoutRow[]>("/api/v1/live-layouts"),
  save: (name: string, payload: { grid?: number; cols?: number }) =>
    api("/api/v1/live-layouts", { method: "POST", body: JSON.stringify({ name, payload }) }),
  remove: (name: string) =>
    api(`/api/v1/live-layouts/${encodeURIComponent(name)}`, { method: "DELETE" }),
};

export interface OnvifEnumeratedStream {
  profile_token: string;
  profile_name: string;
  width: number;
  height: number;
  codec: string;
  uri: string;
}

export interface OnvifEnumerateResponse {
  device_service_url: string;
  media_service_url: string;
  onvif_host: string;
  onvif_port: number;
  device: { manufacturer: string; model: string; firmware: string; serial: string } | null;
  streams: OnvifEnumeratedStream[];
}

export const Onvif = {
  discover: () => api<Array<{ endpoint: string; xaddrs: string; types: string; scopes: string }>>(
                    "/api/v1/onvif/discover"),
  enumerateStreams: (body: { device_url?: string; xaddrs?: string; host?: string; port?: number; user?: string; pass?: string }) =>
    api<OnvifEnumerateResponse>("/api/v1/onvif/enumerate-streams", {
      method: "POST",
      body: JSON.stringify(body),
    }),
  deviceTimeSync: (body: { device_url?: string; xaddrs?: string; host?: string; port?: number; user?: string; pass?: string }) =>
    api<{ ok: boolean }>("/api/v1/onvif/device-time-sync", { method: "POST", body: JSON.stringify(body) }),
  ptz:      (cam_id: string, body: { pan?: number; tilt?: number; zoom?: number; stop?: boolean; profile?: string }) =>
    api(`/api/v1/onvif/${cam_id}/ptz`, { method: "POST", body: JSON.stringify(body) }),
};

export const Setup = {
  status:   () => api<{ first_run: boolean }>("/api/v1/setup/status"),
  finalize: (body: { hostname?: string; admin_password?: string; tz?: string; archive_root?: string },
              setup_token: string) =>
    api("/api/v1/setup/finalize", {
      method: "POST",
      body: JSON.stringify(body),
      headers: { "X-Setup-Token": setup_token },
    }),
};

export const Kiosk = {
  issueToken: () => api<{ token: string }>("/api/v1/kiosk/token", { method: "POST" }),
  exchange:   (token: string) => fetch("/api/v1/kiosk/exchange", {
    method: "POST",
    credentials: "include",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ token }),
  }).then(async (r) => {
    if (!r.ok) throw new Error("kiosk_exchange_failed");
    const body = await r.json();
    _accessToken = body.access_token || body.token;
    if (_accessToken) {
      sessionStorage.setItem("nvr.access", _accessToken);
      useAuth.setState({ token: _accessToken });
    }
    return body;
  }),
};
