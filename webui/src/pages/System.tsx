import { useState, useEffect } from "react";
import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { System, Kiosk, License, useAuth } from "../lib/api";
import { Tabs, Button, Card, CardBody, Field, Input, Select, Textarea, toast, Badge } from "../components/ui";

export function SystemPage() {
  const { t } = useTranslation();
  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.system")}</h1>
      <Tabs tabs={[
        { id: "info", label: t("system.info"), content: <Info /> },
        { id: "lic", label: t("system.license_tab"), content: <LicensePanel /> },
        { id: "net",  label: t("system.network"), content: <Network /> },
        { id: "time", label: t("system.time"), content: <Time /> },
        { id: "svc",  label: t("system.services"), content: <Services /> },
        { id: "log",  label: t("system.logs"), content: <Logs /> },
        { id: "kiosk", label: "Kiosk", content: <KioskPanel /> },
        { id: "pwr",  label: t("system.power"), content: <Power /> },
      ]} />
    </div>
  );
}

function Info() {
  const q = useQuery({ queryKey: ["sysinfo"], queryFn: System.info, refetchInterval: 5000 });
  const role = useAuth(s => s.role);
  const { t } = useTranslation();
  const [bundle, setBundle] = useState("");
  if (q.isLoading) return <div className="text-neutral-500">loading...</div>;
  if (!q.data) return null;
  return (
    <Card><CardBody>
      <dl className="grid grid-cols-2 md:grid-cols-3 gap-2 text-sm">
        <KV k="hostname" v={q.data.hostname} />
        <KV k="version"  v={q.data.version} />
        <KV k="uptime"   v={Math.round(Number(q.data.uptime)/3600) + " h"} />
        <KV k="cpu"      v={q.data.cpu_model.trim()} />
        <KV k="memory"   v={`${Math.round(q.data.memory_kb_total/1024)} MiB`} />
        <KV k="cameras"  v={String(q.data.cameras_active)} />
        {q.data.cpu_temps_c?.[0] && <KV k="cpu_temp" v={`${q.data.cpu_temps_c[0].toFixed(1)} C`} />}
        <KV k="time"     v={q.data.time} />
      </dl>
      {role === "admin" && (
        <div className="mt-4 space-y-2">
          <Button size="sm" variant="secondary" onClick={async () => {
            try {
              const d = await System.fieldBundle();
              setBundle(JSON.stringify(d, null, 2));
            } catch (e: unknown) {
              toast((e as Error).message, "danger");
            }
          }}>{t("system.field_bundle")}</Button>
          {bundle && <pre className="text-xs bg-neutral-950 p-2 rounded max-h-64 overflow-auto">{bundle}</pre>}
        </div>
      )}
    </CardBody></Card>
  );
}
const KV = ({ k, v }: { k: string; v: string }) => (
  <div className="flex"><span className="text-neutral-500 w-28">{k}</span><span className="flex-1">{v}</span></div>
);

function Network() {
  const q = useQuery<any>({ queryKey: ["net"], queryFn: System.network });
  const [yml, setYml] = useState("");
  const save = async () => {
    // Applying netplan can knock the user off the network if the YAML is bad,
    // so require an explicit confirmation. There is no automatic rollback.
    if (!confirm("Apply netplan? A bad config can disconnect you from the appliance.")) return;
    try { await System.setNetwork(yml); toast("ok", "success"); }
    catch (e: any) { toast(e.message, "danger"); }
  };
  return (
    <Card><CardBody className="space-y-3">
      <Field label="ip addr (json)">
        <pre className="text-xs bg-neutral-950 p-2 rounded max-h-72 overflow-auto">{JSON.stringify(q.data, null, 2)}</pre>
      </Field>
      <Field label="netplan YAML"><Textarea rows={10} value={yml} onChange={e => setYml(e.target.value)} placeholder="network:\n  ..." /></Field>
      <Button onClick={save}>apply netplan</Button>
    </CardBody></Card>
  );
}

function Time() {
  const q = useQuery({ queryKey: ["tm"], queryFn: System.time });
  const [tz, setTz] = useState("Europe/Moscow");
  return (
    <Card><CardBody className="space-y-2">
      <pre className="text-xs bg-neutral-950 p-2 rounded">{q.data?.raw}</pre>
      <div className="flex gap-2 items-end">
        <Field label="timezone"><Input value={tz} onChange={e => setTz(e.target.value)} /></Field>
        <Button onClick={async () => { try { await System.setTime(tz); toast("ok", "success"); } catch (e: any) { toast(e.message, "danger"); } }}>set</Button>
      </div>
    </CardBody></Card>
  );
}

function Services() {
  const services = ["nvr-prototype", "nvr-kiosk", "nginx", "chrony"];
  const [out, setOut] = useState<Record<string, string>>({});
  const act = async (name: string, a: "start" | "stop" | "restart" | "status") => {
    if ((a === "stop" || a === "restart") && !confirm(`${a} ${name}?`)) return;
    try { const r = await System.service(name, a); setOut({ ...out, [name]: r.output }); }
    catch (e: any) { toast(e.message, "danger"); }
  };
  return (
    <Card><CardBody className="space-y-2">
      {services.map(s => (
        <div key={s} className="flex gap-2 items-center">
          <span className="font-mono w-32">{s}</span>
          <Button size="sm" variant="ghost" onClick={() => act(s, "status")}>status</Button>
          <Button size="sm" variant="ghost" onClick={() => act(s, "start")}>start</Button>
          <Button size="sm" variant="ghost" onClick={() => act(s, "stop")}>stop</Button>
          <Button size="sm" variant="ghost" onClick={() => act(s, "restart")}>restart</Button>
          {out[s] && <pre className="text-xs ml-2 text-neutral-400 truncate max-w-[300px]">{out[s].slice(0, 200)}</pre>}
        </div>
      ))}
    </CardBody></Card>
  );
}

function Logs() {
  const [unit, setUnit] = useState("nvr-prototype");
  const [lines, setLines] = useState(200);
  const q = useQuery({ queryKey: ["logs", unit, lines], queryFn: () => System.logs(unit, lines) });
  return (
    <Card><CardBody className="space-y-2">
      <div className="flex gap-2">
        <Select value={unit} onChange={e => setUnit(e.target.value)} className="w-auto">
          <option>nvr-prototype</option><option>nvr-kiosk</option><option>nginx</option><option>chrony</option>
        </Select>
        <Input type="number" value={lines} onChange={e => setLines(Number(e.target.value))} className="w-32"/>
        <Button onClick={() => q.refetch()}>refresh</Button>
      </div>
      <pre className="text-xs bg-neutral-950 p-2 rounded max-h-[600px] overflow-auto">{q.data?.log}</pre>
    </CardBody></Card>
  );
}

function LicensePanel() {
  const { t } = useTranslation();
  const q = useQuery({ queryKey: ["license"], queryFn: License.get, refetchInterval: 30000 });
  if (q.isLoading) return <div className="text-neutral-500">{t("common.loading")}</div>;
  if (q.isError) return <div className="text-red-400 text-sm">{(q.error as Error).message}</div>;
  const L = q.data;
  if (!L) return null;
  const tone = L.mode === "licensed" ? "success" : L.mode === "trial" ? "warn" : "danger";
  return (
    <Card><CardBody className="space-y-3 text-sm">
      <div className="flex items-center gap-2">
        <span className="text-neutral-500 w-44 shrink-0">{t("license.mode")}</span>
        <Badge tone={tone}>{t(`license.modes.${L.mode}`)}</Badge>
      </div>
      <div className="flex"><span className="text-neutral-500 w-44 shrink-0">{t("license.max_channels")}</span>{L.max_channels}</div>
      <div className="flex"><span className="text-neutral-500 w-44 shrink-0">{t("license.configured")}</span>{L.channels_configured}</div>
      <div className="flex"><span className="text-neutral-500 w-44 shrink-0">{t("license.modules")}</span>{L.modules_bitmask}</div>
      <div className="flex"><span className="text-neutral-500 w-44 shrink-0">{t("license.signature_ok")}</span>{L.signature_ok ? "✓" : "—"}</div>
      <div className="flex"><span className="text-neutral-500 w-44 shrink-0">fingerprint</span><span className="font-mono text-xs">{L.fingerprint_preview}…</span></div>
      <div className="flex"><span className="text-neutral-500 w-44 shrink-0">{t("license.expires")}</span>{L.expires_at ?? t("license.never")}</div>
      <p className="text-xs text-neutral-500 pt-2 border-t border-neutral-800">{t("license.doc_hint")}</p>
    </CardBody></Card>
  );
}

function KioskPanel() {
  const [tok, setTok] = useState("");
  const issue = async () => { try { const r = await Kiosk.issueToken(); setTok(r.token); } catch (e: any) { toast(e.message, "danger"); } };
  return (
    <Card><CardBody className="space-y-2">
      <Button onClick={issue}>issue kiosk token (180 days)</Button>
      {tok && (
        <div>
          <div className="font-mono text-xs break-all bg-neutral-950 p-2 rounded">{tok}</div>
          <div className="text-xs text-neutral-500 mt-1">
            Save to /etc/nvr-prototype/kiosk.token and restart nvr-kiosk.service. Local UI uses https://nvr.local (nginx TLS on 443).
          </div>
        </div>
      )}
    </CardBody></Card>
  );
}

function Power() {
  return (
    <Card><CardBody className="space-x-2">
      <Button variant="danger" onClick={() => { if (confirm("reboot?")) System.reboot(); }}>reboot</Button>
      <Button variant="danger" onClick={() => { if (confirm("shutdown?")) System.shutdown(); }}>shutdown</Button>
    </CardBody></Card>
  );
}
