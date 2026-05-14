import { useEffect, useRef, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Cameras, Config, Camera } from "../lib/api";
import { Button, Card, CardBody, CardTitle, Field, Input, Select, toast } from "../components/ui";

export function MotionPage() {
  const { t } = useTranslation();
  const qc = useQueryClient();
  const cams = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list });
  const mot  = useQuery({ queryKey: ["motion-cfg"], queryFn: Config.motionGet });
  const [draft, setDraft] = useState(mot.data ?? null);
  useEffect(() => { if (mot.data) setDraft(mot.data); }, [mot.data]);

  const save = async () => {
    if (!draft) return;
    try { await Config.motionPut(draft); toast(t("common.saved"), "success"); }
    catch (e: any) { toast(e.message, "danger"); }
  };

  const [camId, setCamId] = useState<string>("");
  useEffect(() => { if (!camId && cams.data?.length) setCamId(cams.data[0].id); }, [cams.data, camId]);

  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.motion")}</h1>

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("motion.global")}</CardTitle></div>
        <CardBody>
          {draft && (
            <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
              <Field label="downscale_width"><Input type="number" value={draft.downscale_width} onChange={e => setDraft({...draft, downscale_width: Number(e.target.value)})} /></Field>
              <Field label="downscale_height"><Input type="number" value={draft.downscale_height} onChange={e => setDraft({...draft, downscale_height: Number(e.target.value)})} /></Field>
              <Field label="history"><Input type="number" value={draft.history} onChange={e => setDraft({...draft, history: Number(e.target.value)})} /></Field>
              <Field label="var_threshold"><Input type="number" value={draft.var_threshold} onChange={e => setDraft({...draft, var_threshold: Number(e.target.value)})} /></Field>
              <Field label="min_area_ratio"><Input type="number" step="0.001" value={draft.min_area_ratio} onChange={e => setDraft({...draft, min_area_ratio: Number(e.target.value)})} /></Field>
              <Field label="cooldown_seconds"><Input type="number" value={draft.cooldown_seconds} onChange={e => setDraft({...draft, cooldown_seconds: Number(e.target.value)})} /></Field>
              <Field label="snapshot_dir"><Input value={draft.snapshot_dir} onChange={e => setDraft({...draft, snapshot_dir: e.target.value})} /></Field>
              <Field label="jpeg_quality"><Input type="number" value={draft.snapshot_jpeg_quality} onChange={e => setDraft({...draft, snapshot_jpeg_quality: Number(e.target.value)})} /></Field>
              <label className="flex items-center gap-2 col-span-3"><input type="checkbox" checked={draft.detect_shadows} onChange={e => setDraft({...draft, detect_shadows: e.target.checked})} />detect_shadows</label>
              <div className="col-span-3"><Button onClick={save}>{t("common.save")}</Button></div>
            </div>
          )}
        </CardBody>
      </Card>

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800 flex justify-between">
          <CardTitle>{t("motion.roi")}</CardTitle>
          <Select value={camId} onChange={e => setCamId(e.target.value)} className="w-auto">
            {(cams.data ?? []).map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
          </Select>
        </div>
        <CardBody>
          {camId && cams.data && <RoiEditor camera={cams.data.find(c => c.id === camId)!} onSaved={() => qc.invalidateQueries({ queryKey: ["cameras"] })} />}
        </CardBody>
      </Card>
    </div>
  );
}

function RoiEditor({ camera, onSaved }: { camera: Camera; onSaved: () => void }) {
  const { t } = useTranslation();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [rois, setRois] = useState(camera.motion_rois ?? []);
  const [drawing, setDrawing] = useState<Array<[number, number]> | null>(null);

  useEffect(() => { setRois(camera.motion_rois ?? []); }, [camera]);

  const draw = () => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    const img = new Image();
    img.src = `/api/v1/cameras/${camera.id}/snapshot.jpg?_=${Date.now()}`;
    img.onload = () => {
      ctx.drawImage(img, 0, 0, w, h);
      ctx.strokeStyle = "#6366f1"; ctx.lineWidth = 2;
      ctx.fillStyle = "rgba(99,102,241,0.25)";
      for (const r of rois) drawPoly(ctx, r.polygon, w, h);
      if (drawing && drawing.length) drawPoly(ctx, drawing, w, h, true);
    };
  };
  useEffect(draw, [camera, rois, drawing]);

  const onClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const c = canvasRef.current; if (!c) return;
    const rect = c.getBoundingClientRect();
    const x = (e.clientX - rect.left) / rect.width;
    const y = (e.clientY - rect.top)  / rect.height;
    setDrawing(d => [...(d ?? []), [x, y]]);
  };

  const finishPoly = () => {
    if (!drawing || drawing.length < 3) return;
    const name = prompt(t("motion.roi_name") || "ROI name", `roi_${rois.length+1}`);
    if (!name) { setDrawing(null); return; }
    setRois([...rois, { name, polygon: drawing }]);
    setDrawing(null);
  };
  const removeRoi = (i: number) => setRois(rois.filter((_, idx) => idx !== i));

  const save = async () => {
    try { await Cameras.update(camera.id, { motion_rois: rois }); toast(t("common.saved"), "success"); onSaved(); }
    catch (e: any) { toast(e.message, "danger"); }
  };

  return (
    <div className="space-y-3">
      <div className="relative">
        <canvas ref={canvasRef} width={800} height={450} onClick={onClick}
                className="bg-black rounded w-full max-w-3xl border border-neutral-800 cursor-crosshair" />
      </div>
      <div className="flex gap-2">
        <Button variant="secondary" onClick={() => setDrawing([])}>{t("motion.start_poly")}</Button>
        <Button variant="secondary" onClick={finishPoly} disabled={!drawing || drawing.length < 3}>{t("motion.finish_poly")}</Button>
        <Button onClick={save}>{t("common.save")}</Button>
      </div>
      <div className="space-y-1">
        {rois.map((r, i) => (
          <div key={i} className="flex items-center justify-between text-sm">
            <span>{r.name} ({r.polygon.length} pts)</span>
            <Button size="sm" variant="ghost" onClick={() => removeRoi(i)}>{t("common.delete")}</Button>
          </div>
        ))}
      </div>
    </div>
  );
}

function drawPoly(ctx: CanvasRenderingContext2D, poly: Array<[number, number]>, w: number, h: number, isDrawing = false) {
  if (poly.length === 0) return;
  ctx.beginPath();
  ctx.moveTo(poly[0][0] * w, poly[0][1] * h);
  for (let i = 1; i < poly.length; ++i) ctx.lineTo(poly[i][0] * w, poly[i][1] * h);
  if (!isDrawing) ctx.closePath();
  ctx.stroke();
  if (!isDrawing) ctx.fill();
}
