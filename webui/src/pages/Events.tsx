import { useState, useMemo } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Link } from "react-router-dom";
import { Cameras, Events } from "../lib/api";
import { Card, Button, Select, Field, Input, Table, Th, Td, Tr, Badge, toast } from "../components/ui";

export function EventsPage() {
  const { t } = useTranslation();
  const qc = useQueryClient();
  const cams = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list });
  const [cam, setCam]   = useState("");
  const [sev, setSev]   = useState("");
  const [type, setType] = useState("");
  const [hasClip, setHasClip] = useState("");
  const [ackf, setAckf] = useState("");
  const [from, setFrom] = useState("");
  const [to, setTo]     = useState("");

  const q = useMemo(() => {
    const u = new URLSearchParams({ limit: "200" });
    if (cam)  u.set("camera_id", cam);
    if (sev)  u.set("severity", sev);
    if (type) u.set("type", type);
    if (hasClip)  u.set("has_clip", hasClip);
    if (ackf) u.set("ack", ackf);
    if (from) u.set("from", new Date(from).toISOString());
    if (to)   u.set("to",   new Date(to).toISOString());
    return u;
  }, [cam, sev, type, hasClip, ackf, from, to]);

  const ev = useQuery({ queryKey: ["events", q.toString()], queryFn: () => Events.list(q), refetchInterval: 5000 });

  const ack = async (id: number) => {
    try { await Events.ack(id); qc.invalidateQueries({ queryKey: ["events", q.toString()] }); }
    catch (e: any) { toast(e.message, "danger"); }
  };
  const ackAll = async () => {
    if (!confirm(t("events.confirm_ack_all") ?? "Acknowledge all events?")) return;
    try { const r = await Events.ackAll(); toast(`${r.updated}`, "success"); qc.invalidateQueries({ queryKey: ["events"] }); }
    catch (e: any) { toast(e.message, "danger"); }
  };

  return (
    <div className="space-y-3">
      <div className="flex justify-between items-center">
        <h1 className="text-2xl font-semibold">{t("nav.events")}</h1>
        <Button variant="secondary" onClick={ackAll}>{t("events.ack_all")}</Button>
      </div>

      <Card className="p-3">
        <div className="grid grid-cols-2 md:grid-cols-7 gap-2 items-end">
          <Field label={t("archive.camera")}>
            <Select value={cam} onChange={e => setCam(e.target.value)}>
              <option value="">{t("common.all")}</option>
              {(cams.data ?? []).map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
            </Select>
          </Field>
          <Field label={t("events.severity")}>
            <Select value={sev} onChange={e => setSev(e.target.value)}>
              <option value="">{t("common.all")}</option><option>info</option><option>warning</option><option>critical</option>
            </Select>
          </Field>
          <Field label={t("events.type")}><Input value={type} onChange={e => setType(e.target.value)} /></Field>
          <Field label={t("events.has_clip")}>
            <Select value={hasClip} onChange={e => setHasClip(e.target.value)}>
              <option value="">{t("common.all")}</option><option value="true">yes</option><option value="false">no</option>
            </Select>
          </Field>
          <Field label={t("events.ack")}>
            <Select value={ackf} onChange={e => setAckf(e.target.value)}>
              <option value="">{t("common.all")}</option><option value="true">ack</option><option value="false">new</option>
            </Select>
          </Field>
          <Field label={t("archive.from")}><Input type="datetime-local" value={from} onChange={e => setFrom(e.target.value)} /></Field>
          <Field label={t("archive.to")}>  <Input type="datetime-local" value={to}   onChange={e => setTo(e.target.value)} /></Field>
        </div>
      </Card>

      <Card>
        <Table>
          <thead><tr>
            <Th>{t("events.time")}</Th><Th>{t("archive.camera")}</Th><Th>{t("events.type")}</Th>
            <Th>{t("events.severity")}</Th><Th>{t("events.snapshot")}</Th><Th></Th>
          </tr></thead>
          <tbody>
            {(ev.data ?? []).map(e => (
              <Tr key={e.id} className={e.acknowledged ? "" : "bg-amber-950/30"}>
                <Td className="font-mono text-xs">{e.ts.replace("T"," ").substring(0,19)}</Td>
                <Td className="font-mono text-xs">{e.camera_id}</Td>
                <Td>{e.type}</Td>
                <Td>
                  <Badge tone={e.severity === "critical" ? "danger" : e.severity === "warning" ? "warn" : "info"}>
                    {e.severity}
                  </Badge>
                </Td>
                <Td>
                  {e.snapshot_path && (
                    <a href={e.snapshot_path} target="_blank" rel="noreferrer">
                      <img src={e.snapshot_path} className="h-10 rounded" alt="snap" />
                    </a>
                  )}
                </Td>
                <Td className="text-right space-x-1">
                  {e.clip_path && <Link to={`/archive?camera=${e.camera_id}`} className="text-indigo-400 text-xs hover:underline">clip</Link>}
                  {!e.acknowledged && <Button size="sm" variant="ghost" onClick={() => ack(e.id)}>{t("events.ack")}</Button>}
                </Td>
              </Tr>
            ))}
          </tbody>
        </Table>
      </Card>
    </div>
  );
}
