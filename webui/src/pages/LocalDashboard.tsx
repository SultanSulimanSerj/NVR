import { useCallback, useEffect, useMemo, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useNavigate, useSearchParams } from "react-router-dom";
import { useTranslation } from "react-i18next";
import { Cameras, Kiosk, LiveLayouts, useAuth } from "../lib/api";
import { LiveTile } from "../components/LiveTile";
import { Button } from "../components/ui";

const PRESETS_LS = "nvr.live_grid_presets_v1";

interface GridPreset {
  id: string;
  name: string;
  grid: number;
  cols: number;
}

function readPresets(): GridPreset[] {
  try {
    const raw = localStorage.getItem(PRESETS_LS);
    if (!raw) return [];
    const p = JSON.parse(raw) as unknown;
    if (!Array.isArray(p)) return [];
    return p
      .filter(
        (x): x is GridPreset =>
          x &&
          typeof x === "object" &&
          typeof (x as GridPreset).id === "string" &&
          typeof (x as GridPreset).name === "string" &&
          typeof (x as GridPreset).grid === "number" &&
          typeof (x as GridPreset).cols === "number",
      )
      .slice(0, 24);
  } catch {
    return [];
  }
}

function writePresets(list: GridPreset[]) {
  localStorage.setItem(PRESETS_LS, JSON.stringify(list));
}

export function LocalDashboard() {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const qc = useQueryClient();
  const [sp, setSp] = useSearchParams();
  const [bootstrapped, setBootstrapped] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [presets, setPresets] = useState<GridPreset[]>(() => readPresets());

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    const bootstrap = params.get("kiosk_token");

    if (bootstrap) {
      const url = new URL(window.location.href);
      url.searchParams.delete("kiosk_token");
      window.history.replaceState({}, "", url.toString());

      Kiosk.exchange(bootstrap)
        .then(() => setBootstrapped(true))
        .catch((e: Error) => setError(e.message));
    } else if (useAuth.getState().token) {
      setBootstrapped(true);
    } else {
      navigate("/login", { replace: true });
    }
  }, [navigate]);

  const { data: cams = [] } = useQuery({
    queryKey: ["cameras"],
    queryFn: Cameras.list,
    enabled: bootstrapped,
    refetchInterval: 30000,
  });

  const layoutsQ = useQuery({
    queryKey: ["live-layouts"],
    queryFn: LiveLayouts.list,
    enabled: bootstrapped,
    retry: false,
  });

  useEffect(() => {
    if (!layoutsQ.data) return;
    setPresets(prev => {
      const fromSrv: GridPreset[] = layoutsQ.data!.map(row => ({
        id: `srv-${row.id}`,
        name: row.name,
        grid: typeof row.payload?.grid === "number" ? row.payload.grid : 0,
        cols: typeof row.payload?.cols === "number" ? row.payload.cols : 0,
      }));
      const srvNames = new Set(fromSrv.map(p => p.name));
      const localOnly = prev.filter(p => !p.id.startsWith("srv-"));
      const localFiltered = localOnly.filter(p => !srvNames.has(p.name));
      const next = [...fromSrv, ...localFiltered].slice(0, 24);
      writePresets(next);
      return next;
    });
  }, [layoutsQ.data]);

  const grid = Number(sp.get("grid") || "0");
  const cols = Number(sp.get("cols") || "0");
  const n =
    grid > 0 ? grid : (cols > 0 ? cols : Math.max(1, Math.ceil(Math.sqrt(cams.length || 1))));

  const applyPreset = useCallback(
    (p: GridPreset) => {
      const next = new URLSearchParams(sp);
      if (p.grid > 0) next.set("grid", String(p.grid));
      else next.delete("grid");
      if (p.cols > 0) next.set("cols", String(p.cols));
      else next.delete("cols");
      setSp(next, { replace: true });
    },
    [sp, setSp],
  );

  const saveCurrent = useCallback(async () => {
    const name = window.prompt(t("live_dash.name_prompt"), "");
    if (!name?.trim()) return;
    const g = Number(sp.get("grid") || "0");
    const c = Number(sp.get("cols") || "0");
    const entry: GridPreset = {
      id: typeof crypto !== "undefined" && crypto.randomUUID ? crypto.randomUUID() : String(Date.now()),
      name: name.trim(),
      grid: g,
      cols: c,
    };
    setPresets(prev => {
      const list = [...prev, entry];
      writePresets(list);
      return list;
    });
    try {
      await LiveLayouts.save(name.trim(), { grid: g || undefined, cols: c || undefined });
      void qc.invalidateQueries({ queryKey: ["live-layouts"] });
    } catch {
      /* offline / rights — локальный пресет уже сохранён */
    }
  }, [sp, t, qc]);

  const removePreset = useCallback(
    async (p: GridPreset) => {
      setPresets(prev => {
        const list = prev.filter(x => x.id !== p.id);
        writePresets(list);
        return list;
      });
      if (p.id.startsWith("srv-")) {
        try {
          await LiveLayouts.remove(p.name);
          void qc.invalidateQueries({ queryKey: ["live-layouts"] });
        } catch {
          /* ignore */
        }
      }
    },
    [qc],
  );

  const presetLabel = useCallback(
    (p: GridPreset) => {
      if (p.grid > 0) return `${p.name} (${p.grid}×)`;
      if (p.cols > 0) return `${p.name} (${p.cols} col)`;
      return `${p.name} (${t("live_dash.auto")})`;
    },
    [t],
  );

  const bar = useMemo(
    () => (
      <div
        className="fixed bottom-0 left-0 right-0 z-50 flex flex-wrap items-center gap-1 px-2 py-1.5
                   bg-neutral-950/90 border-t border-neutral-800 text-xs text-neutral-300"
      >
        <span className="text-neutral-500 mr-1 shrink-0">{t("live_dash.presets")}</span>
        <Button size="sm" variant="secondary" className="!py-0.5 !px-2" onClick={() => void saveCurrent()}>
          {t("live_dash.save")}
        </Button>
        <span className="text-neutral-600 mx-1">|</span>
        <Button
          size="sm"
          variant="ghost"
          className="!py-0.5 !px-2"
          onClick={() => {
            const next = new URLSearchParams(sp);
            next.delete("grid");
            next.delete("cols");
            setSp(next, { replace: true });
          }}
        >
          {t("live_dash.auto")} ({n})
        </Button>
        {presets.map(p => (
          <span key={p.id} className="inline-flex items-center gap-0.5">
            <Button size="sm" variant="ghost" className="!py-0.5 !px-2" onClick={() => applyPreset(p)}>
              {presetLabel(p)}
            </Button>
            <button
              type="button"
              className="text-neutral-600 hover:text-red-400 px-0.5"
              title={t("live_dash.remove")}
              onClick={() => void removePreset(p)}
            >
              ×
            </button>
          </span>
        ))}
      </div>
    ),
    [applyPreset, n, presetLabel, presets, removePreset, saveCurrent, setSp, sp, t],
  );

  if (error) {
    return (
      <div className="min-h-screen flex items-center justify-center bg-black text-red-400 text-lg">
        kiosk bootstrap failed: {error}
      </div>
    );
  }
  return (
    <div className="min-h-screen pb-10 bg-black">
      <div className="grid gap-1 p-1" style={{ gridTemplateColumns: `repeat(${n}, minmax(0,1fr))` }}>
        {cams.map(c => <LiveTile key={c.id} cameraId={c.id} title={c.name} showOverlay={false} />)}
      </div>
      {bar}
    </div>
  );
}
