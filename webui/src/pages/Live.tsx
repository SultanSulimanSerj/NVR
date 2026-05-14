import { useEffect, useRef, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { useParams } from "react-router-dom";
import { Cameras, Onvif, useAuth } from "../lib/api";
import { LiveTile } from "../components/LiveTile";
import { Card, CardBody, CardTitle, Button, Select, toast } from "../components/ui";

const LAYOUTS = [
  { id: "1x1", cols: 1, rows: 1, max: 1 },
  { id: "2x2", cols: 2, rows: 2, max: 4 },
  { id: "3x3", cols: 3, rows: 3, max: 9 },
  { id: "4x4", cols: 4, rows: 4, max: 16 },
];

export function LivePage() {
  const { t } = useTranslation();
  const { id } = useParams();
  const role = useAuth(s => s.role);
  const cams = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list });
  const [layoutId, setLayoutId] = useState(id ? "1x1" : "2x2");
  const layout = LAYOUTS.find(l => l.id === layoutId)!;
  const [active, setActive] = useState<string | null>(id ?? null);

  const filtered = (cams.data ?? []).slice(0, layout.max);
  if (id) {
    const single = (cams.data ?? []).find(c => c.id === id);
    if (single) return <SingleCamera camera={single} role={role} />;
  }

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-semibold">{t("nav.live")}</h1>
        <Select value={layoutId} onChange={e => setLayoutId(e.target.value)} className="w-auto">
          {LAYOUTS.map(l => <option key={l.id} value={l.id}>{l.id}</option>)}
        </Select>
      </div>
      <div className="grid gap-2" style={{ gridTemplateColumns: `repeat(${layout.cols}, minmax(0, 1fr))` }}>
        {filtered.map(c => (
          <LiveTile key={c.id} cameraId={c.id} title={c.name}
                      active={active === c.id}
                      onClick={() => setActive(c.id)} />
        ))}
      </div>
      {active && cams.data && (
        <PtzPanel camId={active} hasPtz={!!cams.data.find(c => c.id === active)?.onvif_host} role={role} />
      )}
    </div>
  );
}

function SingleCamera({ camera, role }: { camera: any; role: string | null }) {
  return (
    <div className="space-y-3">
      <h1 className="text-xl font-semibold">{camera.name} <span className="text-neutral-500 text-sm font-mono">[{camera.id}]</span></h1>
      <LiveTile cameraId={camera.id} title={camera.name} />
      {camera.onvif_host && <PtzPanel camId={camera.id} hasPtz={true} role={role} />}
    </div>
  );
}

function PtzPanel({ camId, hasPtz, role }: { camId: string; hasPtz: boolean; role: string | null }) {
  if (!hasPtz || (role !== "admin" && role !== "operator")) return null;

  // Track whether we issued a continuous-move so a global pointer-up can stop
  // it. Without this guard, releasing the mouse outside the button (or on a
  // touch device that doesn't fire mouseup) would leave the camera moving.
  const moving = useRef(false);

  const send = async (pan = 0, tilt = 0, zoom = 0) => {
    moving.current = true;
    try { await Onvif.ptz(camId, { pan, tilt, zoom }); }
    catch (e: any) { moving.current = false; toast(e.message, "danger"); }
  };
  const stop = async () => {
    if (!moving.current) return;
    moving.current = false;
    try { await Onvif.ptz(camId, { stop: true }); } catch { /* ignore */ }
  };

  useEffect(() => {
    const onUp = () => { void stop(); };
    window.addEventListener("pointerup",     onUp);
    window.addEventListener("pointercancel", onUp);
    window.addEventListener("blur",          onUp);
    return () => {
      window.removeEventListener("pointerup",     onUp);
      window.removeEventListener("pointercancel", onUp);
      window.removeEventListener("blur",          onUp);
      void stop();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [camId]);

  const snapshot = () => {
    const url = `/api/v1/cameras/${camId}/snapshot.jpg?_=${Date.now()}`;
    const a = document.createElement("a");
    a.href = url; a.download = `${camId}.jpg`; a.target = "_blank";
    document.body.appendChild(a); a.click(); a.remove();
  };
  return (
    <Card>
      <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>PTZ / {camId}</CardTitle></div>
      <CardBody>
        <div className="flex gap-2 items-center">
          <div className="grid grid-cols-3 gap-1 w-32">
            <div />
            <Button size="sm" onPointerDown={() => send(0, 0.5)}>up</Button>
            <div />
            <Button size="sm" onPointerDown={() => send(-0.5, 0)}>left</Button>
            <Button size="sm" variant="secondary" onClick={stop}>stop</Button>
            <Button size="sm" onPointerDown={() => send(0.5, 0)}>right</Button>
            <div />
            <Button size="sm" onPointerDown={() => send(0, -0.5)}>down</Button>
            <div />
          </div>
          <div className="flex flex-col gap-1">
            <Button size="sm" onPointerDown={() => send(0, 0, 0.5)}>zoom +</Button>
            <Button size="sm" onPointerDown={() => send(0, 0, -0.5)}>zoom -</Button>
          </div>
          <div className="ml-auto">
            <Button variant="secondary" onClick={snapshot}>snapshot</Button>
          </div>
        </div>
      </CardBody>
    </Card>
  );
}
