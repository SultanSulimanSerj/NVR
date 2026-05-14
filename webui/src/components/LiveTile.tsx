import { useEffect, useRef } from "react";
import Hls from "hls.js";
import { useAuth } from "../lib/api";

interface Props {
  cameraId: string;
  title?: string;
  showOverlay?: boolean;
  active?: boolean;
  onClick?: () => void;
}

export function LiveTile({ cameraId, title, showOverlay = true, active, onClick }: Props) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const token    = useAuth(s => s.token);

  useEffect(() => {
    const v = videoRef.current;
    if (!v) return;

    // No `?token=` in the URL: the token lives in an Authorization header (hls.js)
    // or in the HttpOnly nvr_token cookie (Safari native HLS, kiosk).
    const src = `/live/${cameraId}/playlist.m3u8`;

    let hls: Hls | null = null;
    let cancelled = false;

    function attachHlsJs() {
      hls = new Hls({
        lowLatencyMode: true,
        liveSyncDuration: 1.5,
        manifestLoadingMaxRetry: 6,
        levelLoadingMaxRetry: 6,
        fragLoadingMaxRetry: 6,
        xhrSetup: (xhr) => {
          xhr.withCredentials = true;
          if (token) xhr.setRequestHeader("Authorization", `Bearer ${token}`);
        },
      });
      hls.on(Hls.Events.ERROR, (_e, data) => {
        if (cancelled || !hls) return;
        if (!data.fatal) return;
        switch (data.type) {
          case Hls.ErrorTypes.NETWORK_ERROR:
            try { hls.startLoad(); } catch { /* ignore */ }
            break;
          case Hls.ErrorTypes.MEDIA_ERROR:
            try { hls.recoverMediaError(); } catch { /* ignore */ }
            break;
          default:
            try { hls.destroy(); } catch { /* ignore */ }
            hls = null;
            setTimeout(() => { if (!cancelled) attachHlsJs(); }, 1500);
        }
      });
      hls.loadSource(src);
      hls.attachMedia(v!);
    }

    if (Hls.isSupported()) {
      attachHlsJs();
    } else if (v.canPlayType("application/vnd.apple.mpegurl")) {
      // Safari / native HLS — relies on the HttpOnly nvr_token cookie issued by
      // /api/v1/auth/login or /api/v1/kiosk/exchange.
      v.src = src;
      v.load();
    }

    return () => {
      cancelled = true;
      if (hls) { try { hls.destroy(); } catch { /* ignore */ } hls = null; }
      v.pause();
      v.removeAttribute("src");
      v.load();
    };
  }, [cameraId, token]);

  return (
    <div onClick={onClick}
      className={`bg-neutral-900 rounded overflow-hidden border ${active ? "border-indigo-500" : "border-neutral-800"} ${onClick ? "cursor-pointer" : ""}`}>
      {showOverlay && (
        <div className="px-2 py-1 text-xs text-neutral-400 flex justify-between">
          <span>{title || cameraId}</span>
          <span className="font-mono">{cameraId}</span>
        </div>
      )}
      <video ref={videoRef} className="w-full bg-black aspect-video" autoPlay muted playsInline />
    </div>
  );
}
