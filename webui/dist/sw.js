// Service worker for the NVR appliance UI.
//
// Build-time the Vite plugin replaces "0.2.0-2026-05-14T16-33-48-146Z" with the git sha (or a
// timestamped fallback). Anything cached under an older name gets evicted on
// activation, so cache-busting is automatic on every release.
const BUILD = "0.2.0-2026-05-14T16-33-48-146Z";
const CACHE = "nvr-static-" + BUILD;
const PRECACHE = ["/", "/web/manifest.webmanifest"];

self.addEventListener("install", (event) => {
  // Take over immediately so the first navigation post-deploy hits the new SW.
  self.skipWaiting();
  event.waitUntil(caches.open(CACHE).then((c) => c.addAll(PRECACHE)));
});

self.addEventListener("activate", (event) => {
  event.waitUntil((async () => {
    const keys = await caches.keys();
    await Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)));
    await self.clients.claim();
    const cs = await self.clients.matchAll({ includeUncontrolled: true });
    for (const client of cs) {
      // Let the SPA show an "update available" prompt; the page may reload
      // on its own discretion.
      client.postMessage({ type: "nvr:sw-activated", build: BUILD });
    }
  })());
});

self.addEventListener("message", (event) => {
  if (event.data && event.data.type === "nvr:skip-waiting") {
    self.skipWaiting();
  }
});

self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.method !== "GET") return;
  const url = new URL(req.url);
  // Never cache live/api streams or auth — they must always go to origin.
  if (url.pathname.startsWith("/api/")
   || url.pathname.startsWith("/live/")
   || url.pathname === "/sw.js") return;

  event.respondWith((async () => {
    const cached = await caches.match(req);
    if (cached) return cached;
    try {
      const resp = await fetch(req);
      if (resp.ok && (resp.type === "basic" || resp.type === "default")) {
        const copy = resp.clone();
        caches.open(CACHE).then((c) => c.put(req, copy));
      }
      return resp;
    } catch {
      const fallback = await caches.match("/");
      return fallback || Response.error();
    }
  })());
});
