import { useState, useEffect } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Cameras, Onvif, Camera, System, toastMessageForApiError, type OnvifEnumerateResponse } from "../lib/api";
import {
  defaultRecordingSchedule,
  scheduleFromCamera,
  scheduleToPayload,
  type RecordingScheduleForm,
} from "../lib/recordingSchedule";
import { RecordingScheduleEditor } from "../components/RecordingScheduleEditor";
import {
  Button, Card, Dialog, Input, Select, Field,
  Table, Th, Td, Tr, Badge, toast
} from "../components/ui";

// Hide rtsp:// passwords from the table display — RTSP URLs are frequently
// shaped `rtsp://user:secret@host/stream`. Operators see them on every page
// and we don't want the secret to land in a screenshot or screen share.
function maskRtsp(url: string | undefined): string {
  if (!url) return "";
  try {
    const u = new URL(url);
    if (u.password) u.password = "********";
    return u.toString();
  } catch {
    return url.replace(/(:\/\/[^:@/]+:)[^@]+@/, "$1********@");
  }
}

function StreamStatsMini({ id }: { id: string }) {
  const { t } = useTranslation();
  const q = useQuery({
    queryKey: ["stream-stats", id],
    queryFn: () => Cameras.streamStats(id),
    refetchInterval: 3000,
  });
  if (!q.data) return <span className="text-neutral-600">—</span>;
  const st = Number(q.data.state);
  const stateHint =
    st === 1 ? t("cameras.stream_state_decode") : st < 0 ? t("cameras.stream_state_error") : t("cameras.stream_state_idle");
  const drops = Number(q.data.frames_dropped_total) || 0;
  const dec = Number(q.data.decoder_errors_total) || 0;
  const lag = Math.round(Number(q.data.pipeline_lag_ms) || 0);
  const title = `${stateHint} · lag ${lag} ms · in ${q.data.bytes_in_total ?? 0} B · rec ${q.data.bytes_recorded_total ?? 0} B · rtsp_reconn ${q.data.rtsp_reconnects_total ?? 0}`;
  return (
    <span className="text-xs font-mono text-neutral-400 block max-w-[220px] truncate" title={title}>
      {Number(q.data.fps).toFixed(1)} fps · {Math.round(Number(q.data.bitrate_kbps) || 0)} kbps
      <span className="text-neutral-500"> · −{drops} · dec {dec}</span>
    </span>
  );
}

const empty: Partial<Camera> = {
  preferred_hw: "auto",
  analysis_fps: 5,
  enable_motion: true,
  enable_recording: true,
  recording_mode: "continuous",
  enable_substream: true,
  pre_event_seconds: 5,
  post_event_seconds: 10,
  sub_bitrate_kbps: 512,
  sub_width: 640,
  sub_height: 360,
  sub_fps: 15,
  onvif_port: 80,
  recording_schedule: defaultRecordingSchedule(),
  plan_x: -1,
  plan_y: -1,
};

export function CamerasPage() {
  const { t } = useTranslation();
  const qc = useQueryClient();
  const cams = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list });
  const catalog = useQuery({
    queryKey: ["camera-catalog"],
    queryFn: System.cameraCatalog,
    staleTime: 300_000,
  });
  const [edit, setEdit] = useState<Partial<Camera> | null>(null);
  const [importing, setImporting] = useState(false);
  const [onvifFormBusy, setOnvifFormBusy] = useState(false);

  const save = async () => {
    if (!edit) return;
    try {
      const schedForm = (edit.recording_schedule ?? defaultRecordingSchedule()) as RecordingScheduleForm;
      const body: Partial<Camera> = {
        ...edit,
        recording_schedule: scheduleToPayload(schedForm),
      };
      if (edit.id && (cams.data ?? []).some(c => c.id === edit.id)) await Cameras.update(edit.id!, body);
      else await Cameras.create(body);
      toast(t("common.saved"), "success");
      setEdit(null);
      qc.invalidateQueries({ queryKey: ["cameras"] });
      qc.invalidateQueries({ queryKey: ["license"] });
      qc.invalidateQueries({ queryKey: ["sysinfo"] });
    } catch (e: unknown) { toast(toastMessageForApiError(e, t), "danger"); }
  };
  const remove = async (id: string) => {
    if (!confirm(t("cameras.confirm_delete"))) return;
    try { await Cameras.remove(id); qc.invalidateQueries({ queryKey: ["cameras"] }); }
    catch (e: unknown) { toast(toastMessageForApiError(e, t), "danger"); }
  };
  const testRtsp = async () => {
    if (!edit?.rtsp_url) return toast("URL?", "danger");
    try { const r = await Cameras.testRtsp(edit.rtsp_url);
      toast(r.reachable ? `OK ${r.host}:${r.port}` : `${t("cameras.unreachable")}`, r.reachable ? "success" : "danger");
    } catch (e: unknown) { toast(toastMessageForApiError(e, t), "danger"); }
  };

  const onvifFillFromForm = async (mode: "main" | "pair") => {
    if (!edit?.onvif_host?.trim()) {
      toast(t("cameras.onvif_host_required"), "danger");
      return;
    }
    setOnvifFormBusy(true);
    try {
      const r = await Onvif.enumerateStreams({
        host: edit.onvif_host.trim(),
        port: edit.onvif_port ?? 80,
        user: edit.onvif_user ?? "",
        pass: edit.onvif_pass ?? "",
      });
      if (!r.streams.length) {
        toast(t("cameras.onvif_no_streams"), "danger");
        return;
      }
      const main = r.streams[0]!.uri;
      const sub = mode === "pair" && r.streams.length >= 2 ? r.streams[1]!.uri : "";
      setEdit({
        ...edit,
        rtsp_url: main,
        sub_rtsp_url: sub,
        enable_substream: Boolean(sub),
        onvif_host: r.onvif_host,
        onvif_port: r.onvif_port,
      });
      toast(t("cameras.onvif_urls_applied"), "success");
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    } finally {
      setOnvifFormBusy(false);
    }
  };

  const onvifSyncTime = async () => {
    if (!edit?.onvif_host?.trim()) {
      toast(t("cameras.onvif_host_required"), "danger");
      return;
    }
    setOnvifFormBusy(true);
    try {
      const r = await Onvif.deviceTimeSync({
        host: edit.onvif_host.trim(),
        port: edit.onvif_port ?? 80,
        user: edit.onvif_user ?? "",
        pass: edit.onvif_pass ?? "",
      });
      toast(r.ok ? t("cameras.onvif_time_sync_ok") : t("cameras.onvif_time_sync_fail"), r.ok ? "success" : "danger");
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    } finally {
      setOnvifFormBusy(false);
    }
  };

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-semibold">{t("nav.cameras")}</h1>
        <div className="flex gap-2">
          <Button variant="secondary" onClick={() => setImporting(true)}>{t("cameras.onvif_import")}</Button>
          <Button onClick={() => setEdit({ ...empty })}>{t("cameras.add")}</Button>
        </div>
      </div>

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800 text-sm font-medium">{t("cameras.catalog_title")}</div>
        <div className="px-4 py-3 text-xs text-neutral-500 space-y-2">
          <p>{t("cameras.catalog_hint")}</p>
          {catalog.data?.source && (
            <p className="font-mono text-neutral-400 break-all">{catalog.data.source}</p>
          )}
          {catalog.isLoading && <p>{t("common.loading")}</p>}
          {catalog.isError && <p className="text-red-400">{(catalog.error as Error)?.message}</p>}
          {!catalog.isLoading && catalog.data && (!catalog.data.loaded || catalog.data.items.length === 0) && (
            <p>{t("cameras.catalog_empty")}</p>
          )}
          {catalog.data && catalog.data.items.length > 0 && (
            <Table>
              <thead>
                <tr>
                  <Th>{t("cameras.catalog_vendor")}</Th>
                  <Th>{t("cameras.catalog_model")}</Th>
                  <Th>{t("cameras.catalog_notes")}</Th>
                </tr>
              </thead>
              <tbody>
                {catalog.data.items.map((row, i) => (
                  <Tr key={i}>
                    <Td className="text-xs">{String(row.vendor ?? "")}</Td>
                    <Td className="text-xs">{String(row.model ?? "")}</Td>
                    <Td className="text-xs text-neutral-400 max-w-md">
                      {[row.firmware_notes, row.onvif_events, row.rtsp_notes].filter(Boolean).join(" · ")}
                    </Td>
                  </Tr>
                ))}
              </tbody>
            </Table>
          )}
        </div>
      </Card>

      <Card>
        <Table>
          <thead><tr>
            <Th>ID</Th><Th>{t("cameras.name")}</Th><Th>RTSP</Th><Th>HW</Th><Th>{t("cameras.features")}</Th>
            <Th>{t("cameras.stream_stats")}</Th><Th></Th>
          </tr></thead>
          <tbody>
            {(cams.data ?? []).map(c => (
              <Tr key={c.id}>
                <Td className="font-mono text-xs">{c.id}</Td>
                <Td>{c.name}</Td>
                <Td className="text-xs text-neutral-400 truncate max-w-[260px]" title={maskRtsp(c.rtsp_url)}>
                  {maskRtsp(c.rtsp_url)}
                </Td>
                <Td><Badge>{c.preferred_hw}</Badge></Td>
                <Td className="space-x-1">
                  {c.enable_recording && (
                    <Badge tone="info">
                      {c.recording_mode === "motion" ? "MOT-REC" : c.recording_mode === "hybrid" ? "HYB-REC" : "REC"}
                    </Badge>
                  )}
                  {c.enable_recording && c.recording_schedule && c.recording_schedule.always === false && (
                    <Badge>{t("cameras.schedule_badge")}</Badge>
                  )}
                  {c.enable_motion    && <Badge tone="warn">MOT</Badge>}
                  {c.enable_substream && <Badge tone="success">SUB</Badge>}
                  {c.onvif_host       && <Badge tone="info">PTZ</Badge>}
                </Td>
                <Td><StreamStatsMini id={c.id} /></Td>
                <Td className="text-right">
                  <Button size="sm" variant="ghost" onClick={() => setEdit({
                    ...c,
                    recording_schedule: scheduleFromCamera(c),
                  })}>{t("common.edit")}</Button>
                  <Button size="sm" variant="ghost" onClick={() => remove(c.id)}>{t("common.delete")}</Button>
                </Td>
              </Tr>
            ))}
          </tbody>
        </Table>
      </Card>

      <Dialog open={!!edit} onClose={() => setEdit(null)} title={t("cameras.edit_camera")} width="max-w-4xl">
        {edit && (
          <div className="grid grid-cols-2 gap-3">
            <Field label="ID"><Input value={edit.id ?? ""} onChange={e => setEdit({...edit, id: e.target.value})} /></Field>
            <Field label={t("cameras.name")}><Input value={edit.name ?? ""} onChange={e => setEdit({...edit, name: e.target.value})} /></Field>
            <div className="col-span-2 flex gap-2">
              <div className="flex-1"><Field label="RTSP URL">
                <Input value={edit.rtsp_url ?? ""} onChange={e => setEdit({...edit, rtsp_url: e.target.value})} />
              </Field></div>
              <div className="self-end"><Button size="sm" variant="secondary" onClick={testRtsp}>{t("cameras.test")}</Button></div>
            </div>
            <Field label={t("cameras.sub_rtsp_url")}>
              <Input value={edit.sub_rtsp_url ?? ""} onChange={e => setEdit({...edit, sub_rtsp_url: e.target.value})} />
            </Field>
            <Field label={t("cameras.preferred_hw")}>
              <Select value={edit.preferred_hw} onChange={e => setEdit({...edit, preferred_hw: e.target.value})}>
                <option>auto</option><option>qsv</option><option>vaapi</option><option>cuda</option><option>cpu</option>
              </Select>
            </Field>
            <Field label="Analysis FPS"><Input type="number" value={edit.analysis_fps}
              onChange={e => setEdit({...edit, analysis_fps: Number(e.target.value)})} /></Field>
            <Field label={t("cameras.sub_bitrate")}><Input type="number" value={edit.sub_bitrate_kbps}
              onChange={e => setEdit({...edit, sub_bitrate_kbps: Number(e.target.value)})} /></Field>
            <div className="grid grid-cols-3 col-span-2 gap-3">
              <Field label="Sub W"><Input type="number" value={edit.sub_width} onChange={e => setEdit({...edit, sub_width: Number(e.target.value)})} /></Field>
              <Field label="Sub H"><Input type="number" value={edit.sub_height} onChange={e => setEdit({...edit, sub_height: Number(e.target.value)})} /></Field>
              <Field label="Sub FPS"><Input type="number" value={edit.sub_fps} onChange={e => setEdit({...edit, sub_fps: Number(e.target.value)})} /></Field>
            </div>
            <Field label="Pre-event sec"><Input type="number" value={edit.pre_event_seconds}
              onChange={e => setEdit({...edit, pre_event_seconds: Number(e.target.value)})} /></Field>
            <Field label="Post-event sec"><Input type="number" value={edit.post_event_seconds}
              onChange={e => setEdit({...edit, post_event_seconds: Number(e.target.value)})} /></Field>
            <Field label="ONVIF host"><Input value={edit.onvif_host ?? ""} onChange={e => setEdit({...edit, onvif_host: e.target.value})} /></Field>
            <Field label="ONVIF port"><Input type="number" value={edit.onvif_port ?? 80} onChange={e => setEdit({...edit, onvif_port: Number(e.target.value)})} /></Field>
            <Field label="ONVIF user"><Input value={edit.onvif_user ?? ""} onChange={e => setEdit({...edit, onvif_user: e.target.value})} /></Field>
            <Field label="ONVIF pass"><Input type="password" value={edit.onvif_pass ?? ""} onChange={e => setEdit({...edit, onvif_pass: e.target.value})} /></Field>
            <div className="col-span-2 flex flex-wrap gap-2">
              <Button type="button" size="sm" variant="secondary" disabled={onvifFormBusy}
                onClick={() => void onvifFillFromForm("main")}>
                {onvifFormBusy ? t("common.loading") : t("cameras.onvif_fill_from_form_main")}
              </Button>
              <Button type="button" size="sm" variant="secondary" disabled={onvifFormBusy}
                onClick={() => void onvifFillFromForm("pair")}>
                {onvifFormBusy ? t("common.loading") : t("cameras.onvif_fill_from_form_pair")}
              </Button>
              <Button type="button" size="sm" variant="secondary" disabled={onvifFormBusy}
                onClick={() => void onvifSyncTime()}>
                {onvifFormBusy ? t("common.loading") : t("cameras.onvif_sync_time")}
              </Button>
            </div>
            <label className="flex items-center gap-2 col-span-2"><input type="checkbox" checked={edit.enable_motion ?? false}
              onChange={e => setEdit({...edit, enable_motion: e.target.checked})} />{t("cameras.enable_motion")}</label>
            <label className="flex items-center gap-2 col-span-2"><input type="checkbox" checked={edit.enable_recording ?? false}
              onChange={e => setEdit({...edit, enable_recording: e.target.checked})} />{t("cameras.enable_recording")}</label>
            <div className="col-span-2">
            <Field label={t("cameras.recording_mode")}>
              <Select value={edit.recording_mode ?? "continuous"}
                onChange={e => {
                  const recording_mode = e.target.value as "continuous" | "motion" | "hybrid";
                  setEdit({
                    ...edit,
                    recording_mode,
                    enable_motion:
                      recording_mode === "motion" || recording_mode === "hybrid"
                        ? true
                        : (edit.enable_motion ?? false),
                  });
                }}>
                <option value="continuous">{t("cameras.recording_continuous")}</option>
                <option value="motion">{t("cameras.recording_motion")}</option>
                <option value="hybrid">{t("cameras.recording_hybrid")}</option>
              </Select>
            </Field>
            </div>
            <label className="flex items-center gap-2 col-span-2"><input type="checkbox" checked={edit.enable_substream ?? false}
              onChange={e => setEdit({...edit, enable_substream: e.target.checked})} />{t("cameras.enable_substream")}</label>

            <div className="col-span-2">
              <RecordingScheduleEditor
                value={(edit.recording_schedule ?? defaultRecordingSchedule()) as RecordingScheduleForm}
                onChange={rs => setEdit({ ...edit, recording_schedule: rs })}
              />
            </div>
            <div className="grid grid-cols-2 col-span-2 gap-3">
              <Field label="plan_x (0…1, −1 вне карты)">
                <Input type="number" step="0.01"
                  value={edit.plan_x ?? -1}
                  onChange={e => setEdit({ ...edit, plan_x: Number(e.target.value) })} />
              </Field>
              <Field label="plan_y">
                <Input type="number" step="0.01"
                  value={edit.plan_y ?? -1}
                  onChange={e => setEdit({ ...edit, plan_y: Number(e.target.value) })} />
              </Field>
            </div>

            <div className="col-span-2 flex justify-end gap-2 mt-2">
              <Button variant="secondary" onClick={() => setEdit(null)}>{t("common.cancel")}</Button>
              <Button onClick={save}>{t("common.save")}</Button>
            </div>
          </div>
        )}
      </Dialog>

      <OnvifImport
        open={importing}
        onClose={() => setImporting(false)}
        onApplyToForm={(partial) => {
          setImporting(false);
          setEdit({
            ...empty,
            ...partial,
            recording_schedule: partial.recording_schedule ?? defaultRecordingSchedule(),
          });
        }}
        onQuickCreate={async (partial) => {
          const schedForm = (partial.recording_schedule ?? defaultRecordingSchedule()) as RecordingScheduleForm;
          const body: Partial<Camera> = {
            ...empty,
            ...partial,
            recording_schedule: scheduleToPayload(schedForm),
          };
          await Cameras.create(body);
          toast(t("cameras.onvif_created"), "success");
          qc.invalidateQueries({ queryKey: ["cameras"] });
          qc.invalidateQueries({ queryKey: ["license"] });
          qc.invalidateQueries({ queryKey: ["sysinfo"] });
          setImporting(false);
        }}
      />
    </div>
  );
}

function buildOnvifCameraPartial(
  r: OnvifEnumerateResponse,
  ou: string,
  op: string,
  mainUri: string,
  subUri?: string,
): Partial<Camera> {
  const nameFromDev =
    r.device && (r.device.manufacturer || r.device.model)
      ? [r.device.manufacturer, r.device.model].filter(Boolean).join(" ").trim()
      : "";
  return {
    id: r.onvif_host.replace(/\./g, "_"),
    name: nameFromDev || r.onvif_host,
    rtsp_url: mainUri,
    sub_rtsp_url: subUri ?? "",
    onvif_host: r.onvif_host,
    onvif_port: r.onvif_port,
    onvif_user: ou,
    onvif_pass: op,
    enable_substream: Boolean(subUri),
  };
}

function OnvifImport({
  open,
  onClose,
  onApplyToForm,
  onQuickCreate,
}: {
  open: boolean;
  onClose: () => void;
  onApplyToForm: (partial: Partial<Camera>) => void;
  onQuickCreate: (partial: Partial<Camera>) => Promise<void>;
}) {
  const { t } = useTranslation();
  type Picked = { xaddrs: string; displayHost: string };
  const [step, setStep] = useState<"list" | "auth">("list");
  const [picked, setPicked] = useState<Picked | null>(null);
  const [ou, setOu] = useState("");
  const [op, setOp] = useState("");
  const [busy, setBusy] = useState(false);
  const [enumRes, setEnumRes] = useState<OnvifEnumerateResponse | null>(null);

  useEffect(() => {
    if (!open) {
      setStep("list");
      setPicked(null);
      setOu("");
      setOp("");
      setEnumRes(null);
      setBusy(false);
    }
  }, [open]);

  const q = useQuery({ queryKey: ["onvif-probe"], queryFn: Onvif.discover, enabled: open && step === "list" });

  const fetchStreams = async () => {
    if (!picked?.xaddrs) return;
    setBusy(true);
    setEnumRes(null);
    try {
      const r = await Onvif.enumerateStreams({ xaddrs: picked.xaddrs, user: ou, pass: op });
      setEnumRes(r);
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    } finally {
      setBusy(false);
    }
  };

  const applyPair = () => {
    if (!enumRes || !enumRes.streams.length) return;
    const main = enumRes.streams[0]!.uri;
    const sub = enumRes.streams.length >= 2 ? enumRes.streams[1]!.uri : undefined;
    onApplyToForm(buildOnvifCameraPartial(enumRes, ou, op, main, sub));
  };

  const applyMainOnly = (uri: string) => {
    if (!enumRes) return;
    onApplyToForm(buildOnvifCameraPartial(enumRes, ou, op, uri));
  };

  const createPair = async () => {
    if (!enumRes || !enumRes.streams.length) return;
    const main = enumRes.streams[0]!.uri;
    const sub = enumRes.streams.length >= 2 ? enumRes.streams[1]!.uri : undefined;
    try {
      await onQuickCreate(buildOnvifCameraPartial(enumRes, ou, op, main, sub));
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    }
  };

  const createMainOnly = async (uri: string) => {
    if (!enumRes) return;
    try {
      await onQuickCreate(buildOnvifCameraPartial(enumRes, ou, op, uri));
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    }
  };

  const applyBestMainOnly = () => {
    if (!enumRes?.streams[0]) return;
    onApplyToForm(buildOnvifCameraPartial(enumRes, ou, op, enumRes.streams[0].uri));
  };

  const createBestMainOnly = async () => {
    if (!enumRes?.streams[0]) return;
    try {
      await onQuickCreate(buildOnvifCameraPartial(enumRes, ou, op, enumRes.streams[0].uri));
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    }
  };

  return (
    <Dialog open={open} onClose={onClose} title={t("cameras.onvif_discover")} width="max-w-3xl">
      {step === "list" && (
        <>
          {q.isLoading && <div className="text-neutral-500">{t("common.loading")}</div>}
          {q.data && q.data.length === 0 && <div className="text-neutral-500">{t("cameras.nothing_found")}</div>}
          <div className="space-y-2 max-h-[50vh] overflow-y-auto">
            {(q.data ?? []).map((d, i) => {
              const m = d.xaddrs.match(/https?:\/\/([^:/]+)/);
              const displayHost = m ? m[1]! : d.endpoint;
              return (
                <button
                  key={i}
                  type="button"
                  onClick={() => {
                    setPicked({ xaddrs: d.xaddrs, displayHost });
                    setStep("auth");
                  }}
                  className="block w-full text-left p-2 bg-neutral-800 hover:bg-neutral-700 rounded"
                >
                  <div className="font-mono text-sm">{displayHost}</div>
                  <div className="text-xs text-neutral-400 truncate">{d.xaddrs}</div>
                </button>
              );
            })}
          </div>
        </>
      )}

      {step === "auth" && picked && (
        <div className="space-y-3">
          <div className="flex items-center justify-between gap-2">
            <div>
              <div className="text-xs text-neutral-500">{t("cameras.onvif_device_label")}</div>
              <div className="font-mono text-sm">{picked.displayHost}</div>
              <div className="text-xs text-neutral-400 truncate" title={picked.xaddrs}>{picked.xaddrs}</div>
            </div>
            <Button variant="secondary" size="sm" type="button" onClick={() => { setStep("list"); setEnumRes(null); }}>
              {t("cameras.onvif_back")}
            </Button>
          </div>

          <div className="text-sm text-neutral-400">{t("cameras.onvif_credentials")}</div>
          <div className="grid grid-cols-2 gap-2">
            <Field label={t("login.user")}>
              <Input value={ou} onChange={e => setOu(e.target.value)} autoComplete="username" />
            </Field>
            <Field label={t("login.password")}>
              <Input type="password" value={op} onChange={e => setOp(e.target.value)} autoComplete="current-password" />
            </Field>
          </div>
          <Button type="button" disabled={busy} onClick={() => void fetchStreams()}>
            {busy ? t("common.loading") : t("cameras.onvif_fetch_rtsp")}
          </Button>

          {enumRes && (
            <div className="space-y-2 border-t border-neutral-800 pt-3">
              {enumRes.device && (
                <div className="text-sm text-neutral-300">
                  {[enumRes.device.manufacturer, enumRes.device.model].filter(Boolean).join(" · ")}
                  {enumRes.device.firmware ? ` · FW ${enumRes.device.firmware}` : ""}
                </div>
              )}
              <div className="text-sm text-neutral-400">{t("cameras.onvif_streams_title")}</div>
              <div className="flex flex-wrap gap-2">
                <Button type="button" size="sm" onClick={applyPair}>{t("cameras.onvif_apply_form")} ({t("cameras.onvif_use_best_pair")})</Button>
                <Button type="button" size="sm" variant="secondary" onClick={() => void createPair()}>{t("cameras.onvif_create_now")} ({t("cameras.onvif_use_best_pair")})</Button>
              </div>
              <div className="flex flex-wrap gap-2">
                <Button type="button" size="sm" onClick={applyBestMainOnly}>{t("cameras.onvif_apply_best_main")}</Button>
                <Button type="button" size="sm" variant="secondary" onClick={() => void createBestMainOnly()}>{t("cameras.onvif_create_best_main")}</Button>
              </div>
              <div className="max-h-[40vh] overflow-y-auto space-y-1">
                {enumRes.streams.map(s => (
                  <div key={s.profile_token} className="flex flex-wrap items-center gap-2 text-xs bg-neutral-900/80 p-2 rounded">
                    <span className="font-mono shrink-0">{s.profile_name || s.profile_token}</span>
                    <span className="text-neutral-500">{s.width}×{s.height} {s.codec}</span>
                    <span className="text-neutral-500 truncate flex-1 min-w-0" title={s.uri}>{maskRtsp(s.uri)}</span>
                    <Button type="button" size="sm" variant="ghost" onClick={() => applyMainOnly(s.uri)}>{t("cameras.onvif_apply_form")}</Button>
                    <Button type="button" size="sm" variant="ghost" onClick={() => void createMainOnly(s.uri)}>{t("cameras.onvif_create_now")}</Button>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>
      )}
    </Dialog>
  );
}
