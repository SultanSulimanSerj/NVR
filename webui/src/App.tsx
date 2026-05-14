import { Navigate, Route, Routes, NavLink, useNavigate } from "react-router-dom";
import { lazy, Suspense, useEffect, useState } from "react";
import { useAuth } from "./lib/api";
import { useTranslation } from "react-i18next";
import { ToastHost, toast } from "./components/ui";
import { RequireRole } from "./components/RequireRole";
import { ErrorBoundary } from "./components/ErrorBoundary";

// Login / first-run / kiosk are tiny and always loaded — putting them behind
// lazy() just adds a flash before the splash.
import { Login }       from "./pages/Login";
import { SetupWizard } from "./pages/Setup";

// Everything else is route-split. Each chunk is ~30–60 KB and lazy-loaded; the
// outer Suspense paints a placeholder before the chunk arrives.
const Dashboard       = lazy(() => import("./pages/Dashboard").then(m => ({ default: m.Dashboard })));
const LivePage        = lazy(() => import("./pages/Live").then(m => ({ default: m.LivePage })));
const ArchivePage     = lazy(() => import("./pages/Archive").then(m => ({ default: m.ArchivePage })));
const EventsPage      = lazy(() => import("./pages/Events").then(m => ({ default: m.EventsPage })));
const CamerasPage     = lazy(() => import("./pages/Cameras").then(m => ({ default: m.CamerasPage })));
const MotionPage      = lazy(() => import("./pages/Motion").then(m => ({ default: m.MotionPage })));
const StoragePage     = lazy(() => import("./pages/Storage").then(m => ({ default: m.StoragePage })));
const UsersPage       = lazy(() => import("./pages/Users").then(m => ({ default: m.UsersPage })));
const SystemPage      = lazy(() => import("./pages/System").then(m => ({ default: m.SystemPage })));
const NotifyPage      = lazy(() => import("./pages/Notifications").then(m => ({ default: m.NotifyPage })));
const DlqPage          = lazy(() => import("./pages/Dlq").then(m => ({ default: m.DlqPage })));
const AiPage          = lazy(() => import("./pages/Ai").then(m => ({ default: m.AiPage })));
const AuditPage       = lazy(() => import("./pages/Audit").then(m => ({ default: m.AuditPage })));
const LocalDashboard  = lazy(() => import("./pages/LocalDashboard").then(m => ({ default: m.LocalDashboard })));
const MapPage          = lazy(() => import("./pages/Map").then(m => ({ default: m.MapPage })));

const RequireAuth = ({ children }: { children: React.ReactNode }) => {
  const token = useAuth((s) => s.token);
  return token ? <>{children}</> : <Navigate to="/login" replace />;
};

const LazyFallback = () => (
  <div className="p-6 text-neutral-500 text-sm animate-pulse">Загрузка…</div>
);

function Shell({ children }: { children: React.ReactNode }) {
  const { t, i18n } = useTranslation();
  const nav   = useNavigate();
  const logout = useAuth((s) => s.setToken);
  const role   = useAuth((s) => s.role);
  const login  = useAuth((s) => s.login);
  const [branding, setBranding] = useState<{ product_name?: string; logo_url?: string }>({});
  const [online, setOnline] = useState(() => (typeof navigator === "undefined" ? true : navigator.onLine));

  useEffect(() => {
    const up = () => setOnline(true);
    const down = () => setOnline(false);
    window.addEventListener("online", up);
    window.addEventListener("offline", down);
    return () => { window.removeEventListener("online", up); window.removeEventListener("offline", down); };
  }, []);

  useEffect(() => {
    fetch("/api/v1/branding").then(r => r.json()).then(setBranding).catch(() => {});
  }, []);

  // Listen for global 401 events emitted by the API client — when the refresh
  // token is also expired we boot the user back to /login.
  useEffect(() => {
    const onUnauthorized = () => { logout(null); nav("/login"); };
    window.addEventListener("nvr:unauthorized", onUnauthorized);
    return () => window.removeEventListener("nvr:unauthorized", onUnauthorized);
  }, [logout, nav]);

  const link   = "px-3 py-2 rounded hover:bg-neutral-800 block text-sm";
  const active = "bg-neutral-800 text-white";

  return (
    <div className="min-h-screen flex bg-neutral-950 text-neutral-100">
      <aside className="w-56 shrink-0 border-r border-neutral-800 p-3 flex flex-col">
        <div className="text-lg font-bold mb-4 flex items-center gap-2">
          {branding.logo_url && <img src={branding.logo_url} className="h-6 w-6" alt="logo"/>}
          {branding.product_name || t("app.name")}
        </div>
        <nav className="space-y-0.5 flex-1">
          <NavLink to="/"             className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.dashboard")}</NavLink>
          <NavLink to="/live"         className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.live")}</NavLink>
          <NavLink to="/archive"      className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.archive")}</NavLink>
          <NavLink to="/events"       className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.events")}</NavLink>
          <NavLink to="/cameras"      className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.cameras")}</NavLink>
          <NavLink to="/map"          className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.map")}</NavLink>
          <NavLink to="/motion"       className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.motion")}</NavLink>
          <NavLink to="/ai"           className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.ai")}</NavLink>
          <NavLink to="/storage"      className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.storage")}</NavLink>
          <NavLink to="/notifications" className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.notifications")}</NavLink>
          <NavLink to="/notifications/dlq" className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.dlq")}</NavLink>
          <NavLink to="/users"        className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.users")}</NavLink>
          <NavLink to="/audit"        className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.audit")}</NavLink>
          <NavLink to="/system"       className={({ isActive }) => `${link} ${isActive ? active : ""}`}>{t("nav.system")}</NavLink>
        </nav>
        <div className="mt-4 pt-3 border-t border-neutral-800 text-xs text-neutral-500 space-y-2">
          <div>{login} <span className="text-indigo-400">[{role}]</span></div>
          <div className="flex gap-1">
            <button onClick={() => i18n.changeLanguage("ru")}
              className={`px-1.5 py-0.5 rounded ${i18n.language === "ru" ? "bg-neutral-800" : ""}`}>RU</button>
            <button onClick={() => i18n.changeLanguage("en")}
              className={`px-1.5 py-0.5 rounded ${i18n.language === "en" ? "bg-neutral-800" : ""}`}>EN</button>
          </div>
          <button className="text-left hover:text-red-400"
            onClick={() => { logout(null); nav("/login"); }}>
            {t("nav.logout")}
          </button>
        </div>
      </aside>
      <main className="flex-1 p-6 overflow-auto">
        {!online && (
          <div className="mb-4 rounded border border-amber-700/60 bg-amber-950/40 px-3 py-2 text-sm text-amber-200">
            {t("app.offline")}
          </div>
        )}
        <ErrorBoundary>
          <Suspense fallback={<LazyFallback />}>
            {children}
          </Suspense>
        </ErrorBoundary>
      </main>
      <ToastHost />
    </div>
  );
}

// Listen for service-worker activation messages and prompt the user to reload
// for the new build. Registration itself happens in main.tsx.
function useServiceWorkerUpdates() {
  useEffect(() => {
    if (!("serviceWorker" in navigator)) return;
    const onMsg = (e: MessageEvent) => {
      if (e.data?.type === "nvr:sw-activated") {
        // Cheap UX: toast that links nowhere; the next navigation will pick up
        // the new assets. Could be elevated to a real banner later.
        toast("Доступна новая версия — перезагрузите страницу", "info");
      }
    };
    navigator.serviceWorker.addEventListener("message", onMsg);
    return () => navigator.serviceWorker.removeEventListener("message", onMsg);
  }, []);
}

export function App() {
  useServiceWorkerUpdates();
  return (
    <Routes>
      <Route path="/login"           element={<Login />} />
      <Route path="/setup"           element={<SetupWizard />} />
      <Route path="/local-dashboard" element={
        <Suspense fallback={<LazyFallback />}><LocalDashboard /></Suspense>
      } />
      <Route path="*"      element={
        <RequireAuth>
          <Shell>
            <Routes>
              <Route path="/"               element={<Dashboard />} />
              <Route path="/live"           element={<LivePage />} />
              <Route path="/live/:id"       element={<LivePage />} />
              <Route path="/archive"        element={<ArchivePage />} />
              <Route path="/events"         element={<EventsPage />} />
              <Route path="/cameras"        element={<RequireRole role="operator"><CamerasPage /></RequireRole>} />
              <Route path="/map"            element={<RequireRole role="viewer"><MapPage /></RequireRole>} />
              <Route path="/motion"         element={<RequireRole role="admin"><MotionPage /></RequireRole>} />
              <Route path="/ai"             element={<RequireRole role="operator"><AiPage /></RequireRole>} />
              <Route path="/storage"        element={<StoragePage />} />
              <Route path="/notifications"  element={<RequireRole role="admin"><NotifyPage /></RequireRole>} />
              <Route path="/notifications/dlq" element={<RequireRole role="admin"><DlqPage /></RequireRole>} />
              <Route path="/users"          element={<RequireRole role="admin"><UsersPage /></RequireRole>} />
              <Route path="/audit"          element={<RequireRole role="admin"><AuditPage /></RequireRole>} />
              <Route path="/system"         element={<RequireRole role="admin"><SystemPage /></RequireRole>} />
            </Routes>
          </Shell>
        </RequireAuth>
      } />
    </Routes>
  );
}
