import React from "react";
import ReactDOM from "react-dom/client";
import { BrowserRouter } from "react-router-dom";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";

import "./i18n";
import "./index.css";
import { App } from "./App";

const qc = new QueryClient({
  defaultOptions: { queries: { refetchOnWindowFocus: false, retry: 1 } },
});

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <BrowserRouter>
      <QueryClientProvider client={qc}>
        <App />
      </QueryClientProvider>
    </BrowserRouter>
  </React.StrictMode>
);

// Register the service worker after the React tree is mounted. We don't block
// the initial paint on it. The SW handles its own activation + skipWaiting.
if ("serviceWorker" in navigator && import.meta.env.PROD) {
  window.addEventListener("load", () => {
    navigator.serviceWorker.register("/sw.js").catch((err) => {
      // eslint-disable-next-line no-console
      console.warn("[sw] register failed:", err);
    });
  });
}
