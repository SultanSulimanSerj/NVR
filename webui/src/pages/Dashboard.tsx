import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Cameras, Events, System, Storage } from "../lib/api";
import { Card, CardBody, CardTitle, Badge } from "../components/ui";
import { Link } from "react-router-dom";

function Kpi({ label, value, sub }: { label: string; value: React.ReactNode; sub?: string }) {
  return (
    <Card>
      <CardBody>
        <div className="text-xs text-neutral-500 uppercase">{label}</div>
        <div className="text-3xl font-semibold mt-1">{value}</div>
        {sub && <div className="text-xs text-neutral-500 mt-1">{sub}</div>}
      </CardBody>
    </Card>
  );
}

export function Dashboard() {
  const { t } = useTranslation();
  const cams   = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list,           refetchInterval: 10000 });
  const ev     = useQuery({ queryKey: ["ev24"],   queryFn: () => Events.list(new URLSearchParams({ limit: "30" })),
                            refetchInterval: 5000 });
  const info   = useQuery({ queryKey: ["sysinfo"], queryFn: System.info,             refetchInterval: 5000 });
  const usage  = useQuery({ queryKey: ["sysuse"], queryFn: Storage.usage,           refetchInterval: 10000 });
  const unack  = (ev.data ?? []).filter(e => !e.acknowledged).length;
  const usedPct = usage.data ? Math.round(usage.data.archive_used_ratio * 100) : 0;

  return (
    <div className="space-y-4">
      <h1 className="text-2xl font-semibold">{t("nav.dashboard")}</h1>
      <div className="grid grid-cols-1 md:grid-cols-4 gap-3">
        <Kpi label={t("dash.cameras")} value={cams.data?.length ?? 0}
             sub={`${info.data?.cameras_active ?? 0} ${t("dash.active")}`} />
        <Kpi label={t("dash.events_24h")} value={ev.data?.length ?? 0}
             sub={`${unack} ${t("dash.unack")}`} />
        <Kpi label={t("dash.archive_usage")} value={`${usedPct}%`} />
        <Kpi label={t("dash.uptime")}
             value={info.data ? Math.round(Number(info.data.uptime) / 3600) + "h" : "-"} />
      </div>

      {info.data?.features?.license && (() => {
        const L = info.data.features.license;
        const tone = L.mode === "licensed" ? "success" : L.mode === "trial" ? "warn" : "danger";
        return (
          <Card>
            <div className="px-4 py-3 border-b border-neutral-800 flex justify-between items-center">
              <CardTitle>{t("dash.license")}</CardTitle>
              <Badge tone={tone}>{t(`license.modes.${L.mode}`)}</Badge>
            </div>
            <CardBody className="text-sm flex flex-wrap gap-x-6 gap-y-2">
              <span><span className="text-neutral-500">{t("license.max_channels")} </span>{L.max_channels}</span>
              <span><span className="text-neutral-500">{t("license.configured")} </span>{L.channels_configured}</span>
              <span><span className="text-neutral-500">{t("license.expires")} </span>{L.expires_at ?? t("license.never")}</span>
            </CardBody>
          </Card>
        );
      })()}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-3">
        <Card>
          <div className="px-4 py-3 border-b border-neutral-800 flex justify-between">
            <CardTitle>{t("dash.recent_events")}</CardTitle>
            <Link to="/events" className="text-xs text-indigo-400 hover:underline">{t("common.all")}</Link>
          </div>
          <CardBody className="max-h-[400px] overflow-auto">
            {(ev.data ?? []).slice(0, 15).map(e => (
              <div key={e.id} className="flex items-center gap-2 py-1.5 text-sm border-b border-neutral-800/40 last:border-0">
                <span className="font-mono text-neutral-500 text-xs">{e.ts.replace("T", " ").substring(0,19)}</span>
                <Badge tone={e.severity === "critical" ? "danger" : e.severity === "warning" ? "warn" : "info"}>
                  {e.severity}
                </Badge>
                <span className="font-mono text-xs">{e.camera_id}</span>
                <span className="text-xs">{e.type}</span>
                {!e.acknowledged && <Badge tone="warn" className="ml-auto">new</Badge>}
              </div>
            ))}
          </CardBody>
        </Card>

        <Card>
          <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("dash.cameras_status")}</CardTitle></div>
          <CardBody className="space-y-2">
            {(cams.data ?? []).map(c => (
              <div key={c.id} className="flex items-center justify-between text-sm">
                <span>{c.name}</span>
                <Link to={`/live/${c.id}`} className="text-indigo-400 text-xs hover:underline">Live</Link>
              </div>
            ))}
          </CardBody>
        </Card>
      </div>

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("dash.host_info")}</CardTitle></div>
        <CardBody>
          {info.isLoading ? <div className="text-neutral-500 text-sm">{t("common.loading")}</div> :
            info.data && (
            <div className="grid grid-cols-2 md:grid-cols-4 gap-3 text-sm">
              <div><span className="text-neutral-500 mr-2">host</span>{info.data.hostname}</div>
              <div><span className="text-neutral-500 mr-2">version</span>{info.data.version}</div>
              <div><span className="text-neutral-500 mr-2">cpu</span>{info.data.cpu_model.trim().slice(0, 28)}</div>
              <div><span className="text-neutral-500 mr-2">memory</span>{Math.round(info.data.memory_kb_total/1024)} MiB</div>
              {info.data.cpu_temps_c?.length > 0 && (
                <div><span className="text-neutral-500 mr-2">temp</span>{info.data.cpu_temps_c[0].toFixed(1)} C</div>
              )}
            </div>
          )}
        </CardBody>
      </Card>
    </div>
  );
}
