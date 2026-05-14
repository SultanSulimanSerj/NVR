import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import fs from "node:fs";
import path from "node:path";

// Bump the build version on every CI build so the service worker invalidates
// caches deterministically (we mix this into the cache name in sw.js).
const pkg = JSON.parse(
  fs.readFileSync(path.resolve(__dirname, "package.json"), "utf8")
);
const buildId =
  process.env.GIT_SHA
  ?? process.env.GITHUB_SHA
  ?? `${pkg.version}-${new Date().toISOString().replace(/[:.]/g, "-")}`;

export default defineConfig({
  plugins: [
    react(),
    {
      name: "nvr-sw-version",
      // Read sw.js from /public, replace the placeholder, write to dist/.
      writeBundle() {
        const src = path.resolve(__dirname, "public/sw.js");
        const dst = path.resolve(__dirname, "dist/sw.js");
        if (!fs.existsSync(src)) return;
        let body = fs.readFileSync(src, "utf8");
        body = body.replace(/__APP_VERSION__/g, JSON.stringify(buildId));
        fs.writeFileSync(dst, body);
      },
    },
  ],
  define: {
    __APP_VERSION__: JSON.stringify(buildId),
  },
  server: {
    port: 5173,
    proxy: {
      "/api":     { target: "http://localhost:8080", changeOrigin: true },
      "/live":    { target: "http://localhost:8080", changeOrigin: true },
      "/metrics": { target: "http://localhost:8080", changeOrigin: true },
    },
  },
  build: {
    outDir: "dist",
    sourcemap: false,
    target: "es2020",
  },
});
