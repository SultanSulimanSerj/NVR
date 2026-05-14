import { useState, useMemo, useCallback, useRef } from "react";
import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Cameras, Archive, Events, useAuth, toastMessageForApiError } from "../lib/api";
import { Card, CardBody, Button, Select, Field, Input, Table, Th, Td, Tr, Badge, toast } from "../components/ui";

function toDatetimeLocalValue(d: Date) {
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

export function ArchivePage() {
  const { t } = useTranslation();
  const role = useAuth(s => s.role);
  const canExport = role === "admin" || role === "operator";
  const cams = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list });
  const [cam, setCam] = useState("");
  const [from, setFrom] = useState("");
  const [to, setTo] = useState("");
  const [activeSeg, setActiveSeg] = useState<number | null>(null);

  const q = useMemo(() => {
    const u = new URLSearchParams();
    if (cam)  u.set("camera_id", cam);
    if (from) u.set("from", new Date(from).toISOString());
    if (to)   u.set("to",   new Date(to).toISOString());
    return u;
  }, [cam, from, to]);

  const tlParams = useMemo(() => {
    if (!cam || !from || !to) return null;
    const u = new URLSearchParams();
    u.set("camera_id", cam);
    u.set("from", new Date(from).toISOString());
    u.set("to", new Date(to).toISOString());
    return u;
  }, [cam, from, to]);

  const segs = useQuery({ queryKey: ["segments", q.toString()], queryFn: () => Archive.segments(q) });
  const tl = useQuery({
    queryKey: ["archive-timeline", tlParams?.toString() ?? ""],
    queryFn: () => Archive.timeline(tlParams!),
    enabled: !!tlParams,
  });

  const clipEventsParams = useMemo(() => {
    if (!cam) return null;
    const u = new URLSearchParams();
    u.set("camera_id", cam);
    u.set("has_clip", "true");
    u.set("limit", "30");
    return u;
  }, [cam]);

  const clipEvents = useQuery({
    queryKey: ["archive", "clip-events", clipEventsParams?.toString() ?? ""],
    queryFn: () => Events.list(clipEventsParams!),
    enabled: !!clipEventsParams,
  });

  const filtersCardRef = useRef<HTMLDivElement>(null);

  const onTimelineBinClick = useCallback((bucketStartIso: string, bucketSec: number) => {
    const start = new Date(bucketStartIso);
    const end = new Date(start.getTime() + bucketSec * 1000);
    setFrom(toDatetimeLocalValue(start));
    setTo(toDatetimeLocalValue(end));
    toast(t("archive.timeline_range_applied"), "success");
    requestAnimationFrame(() =>
      filtersCardRef.current?.scrollIntoView({ behavior: "smooth", block: "start" }),
    );
  }, [t]);

  const onExportRange = useCallback(async () => {
    if (!cam || !from || !to) return;
    try {
      const blob = await Archive.exportRangeBlob(cam, new Date(from).toISOString(), new Date(to).toISOString());
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `export-${cam}.mp4`;
      a.click();
      URL.revokeObjectURL(url);
      toast(t("archive.export_range_done"), "success");
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    }
  }, [cam, from, to, t]);

  const jumpRangeFromEventTs = useCallback((tsIso: string) => {
    const evT = new Date(tsIso);
    const fromD = new Date(evT.getTime() - 45_000);
    const toD = new Date(evT.getTime() + 45_000);
    setFrom(toDatetimeLocalValue(fromD));
    setTo(toDatetimeLocalValue(toD));
    setActiveSeg(null);
  }, []);

  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.archive")}</h1>

      <div ref={filtersCardRef}>
        <Card>
          <CardBody className="grid grid-cols-1 md:grid-cols-4 gap-2 items-end">
          <Field label={t("archive.camera")}>
            <Select value={cam} onChange={e => setCam(e.target.value)}>
              <option value="">{t("common.all")}</option>
              {(cams.data ?? []).map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
            </Select>
          </Field>
          <Field label={t("archive.from")}><Input type="datetime-local" value={from} onChange={e => setFrom(e.target.value)} /></Field>
          <Field label={t("archive.to")}>  <Input type="datetime-local" value={to}   onChange={e => setTo(e.target.value)}   /></Field>
          <div className="flex flex-wrap gap-2">
            <Button onClick={() => { segs.refetch(); void tl.refetch(); }}>{t("common.search")}</Button>
            {canExport && cam && from && to && (
              <Button variant="secondary" onClick={() => void onExportRange()}>{t("archive.export_range")}</Button>
            )}
          </div>
        </CardBody>
      </Card>
      </div>

      {cam && (
        <Card>
          <CardBody>
            <div className="text-sm text-neutral-400 mb-2">{t("archive.recent_clipped_events")}</div>
            {clipEvents.isLoading && <div className="text-neutral-500 text-sm">{t("common.loading")}</div>}
            {clipEvents.isError && <div className="text-red-400 text-sm">{(clipEvents.error as Error)?.message}</div>}
            <Table>
              <thead><tr>
                <Th>{t("events.time")}</Th><Th>{t("events.type")}</Th><Th></Th>
              </tr></thead>
              <tbody>
                {(clipEvents.data ?? []).filter(e => e.clip_path).map(e => (
                  <Tr key={e.id}>
                    <Td className="font-mono text-xs">{e.ts.replace("T", " ").substring(0, 19)}</Td>
                    <Td>{e.type}</Td>
                    <Td className="text-right">
                      <Button size="sm" variant="ghost" onClick={() => jumpRangeFromEventTs(e.ts)}>
                        {t("archive.jump_range")}
                      </Button>
                    </Td>
                  </Tr>
                ))}
              </tbody>
            </Table>
            {clipEvents.data?.filter(e => e.clip_path).length === 0 && !clipEvents.isLoading && (
              <div className="text-neutral-500 text-sm">{t("archive.no_clipped_events")}</div>
            )}
          </CardBody>
        </Card>
      )}

      {tlParams && (
        <Card>
          <CardBody>
            <div className="text-sm text-neutral-400 mb-1">{t("archive.timeline")}</div>
            {tl.isLoading && <div className="text-neutral-500 text-sm">{t("common.loading")}</div>}
            {tl.isError && <div className="text-red-400 text-sm">{(tl.error as Error)?.message}</div>}
            {tl.data && (
              <div className="flex w-full gap-px h-4 rounded overflow-hidden bg-neutral-900 border border-neutral-700">
                {tl.data.buckets.map((b, i) => (
                  <button
                    key={`${b.bucket_start_unix}-${i}`}
                    type="button"
                    title={`${b.bucket_start} · seg ${b.segment_count} · evt ${b.event_count ?? 0}`}
                    className="flex-1 min-w-[2px] min-h-[12px] cursor-pointer hover:opacity-90 flex flex-col gap-px p-0 overflow-hidden rounded-sm border border-neutral-800/50"
                    onClick={() => onTimelineBinClick(b.bucket_start, tl.data.bucket_seconds)}
                  >
                    <div className={`flex-1 min-h-[5px] ${b.has_motion ? "bg-amber-600" : "bg-neutral-800"}`} />
                    <div className={`flex-1 min-h-[5px] ${(b.event_count ?? 0) > 0 ? "bg-indigo-500" : "bg-neutral-950"}`} />
                  </button>
                ))}
              </div>
            )}
            {tl.data?.buckets.length === 0 && (
              <div className="text-neutral-500 text-sm">{t("archive.timeline_empty")}</div>
            )}
            <p className="text-xs text-neutral-500 mt-1">{t("archive.timeline_click_hint")}</p>
            <p className="text-xs text-neutral-500">{t("archive.timeline_legend_motion")}</p>
          </CardBody>
        </Card>
      )}
      {!tlParams && (from || to) && (
        <p className="text-sm text-neutral-500">{t("archive.timeline_hint")}</p>
      )}

      {activeSeg !== null && (
        <Card>
          <CardBody>
            <video controls autoPlay className="w-full max-w-4xl bg-black aspect-video"
                    src={Archive.playUrl(activeSeg)} />
            <div className="mt-2 flex gap-2 flex-wrap">
              <a href={Archive.playUrl(activeSeg)} download className="text-indigo-400 text-sm hover:underline">{t("archive.download")}</a>
              {canExport && (
                <a href={Archive.exportUrl(activeSeg)} className="text-indigo-400 text-sm hover:underline">{t("archive.export")}</a>
              )}
              <Button size="sm" variant="ghost" onClick={() => setActiveSeg(null)}>{t("common.close")}</Button>
            </div>
          </CardBody>
        </Card>
      )}

      <Card>
        <Table>
          <thead><tr>
            <Th>{t("archive.camera")}</Th><Th>{t("archive.started")}</Th>
            <Th>{t("archive.duration")}</Th><Th>{t("archive.size")}</Th>
            <Th>{t("archive.motion")}</Th><Th></Th>
          </tr></thead>
          <tbody>
            {(segs.data ?? []).map(s => (
              <Tr key={s.id}>
                <Td className="font-mono text-xs">{s.camera_id}</Td>
                <Td className="font-mono text-xs">{s.started_at.replace("T"," ").substring(0,19)}</Td>
                <Td>{Math.round(s.duration_ms/1000)} s</Td>
                <Td>{(s.size_bytes / (1024*1024)).toFixed(1)} MiB</Td>
                <Td>{s.has_motion && <Badge tone="warn">motion</Badge>}</Td>
                <Td className="text-right space-x-2">
                  <Button size="sm" variant="ghost" onClick={() => setActiveSeg(s.id)}>{t("archive.play")}</Button>
                  {canExport && (
                    <a href={Archive.exportUrl(s.id)} className="text-sm text-indigo-400 hover:underline">{t("archive.export")}</a>
                  )}
                </Td>
              </Tr>
            ))}
          </tbody>
        </Table>
        {segs.data?.length === 0 && <div className="p-4 text-neutral-500">{t("archive.empty")}</div>}
      </Card>
    </div>
  );
}
