import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { api, Cameras, Events, toastMessageForApiError, type Camera } from "../lib/api";
import { Tabs, Card, CardBody, Button, Field, Input, Select, Table, Th, Td, Tr, toast } from "../components/ui";

interface FacePerson { id: number; name: string; }
interface AiModel { id: number; kind: string; path: string; precision: string; enabled: boolean; }

export function AiPage() {
  const { t } = useTranslation();
  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.ai")}</h1>
      <Tabs tabs={[
        { id: "models", label: t("ai.models"), content: <Models /> },
        { id: "faces",  label: t("ai.faces"),  content: <Faces /> },
        { id: "search", label: t("ai.search"), content: <Search /> },
      ]} />
    </div>
  );
}

function Models() {
  const { t } = useTranslation();
  const q = useQuery({ queryKey: ["ai-models"], queryFn: () => api<AiModel[]>("/api/v1/ai/models").catch(() => []) });
  const [m, setM] = useState({ kind: "person", path: "", precision: "FP16" });
  const create = async () => {
    try {
      await api("/api/v1/ai/models", { method: "POST", body: JSON.stringify(m) });
      toast("ok", "success");
      q.refetch();
    } catch (e: unknown) { toast(toastMessageForApiError(e, t), "danger"); }
  };
  const toggle = async (id: number, enabled: boolean) => {
    try {
      await api(`/api/v1/ai/models/${id}`, { method: "PATCH", body: JSON.stringify({ enabled: !enabled }) });
      q.refetch();
    } catch (e: unknown) { toast(toastMessageForApiError(e, t), "danger"); }
  };
  return (
    <Card><CardBody className="space-y-3">
      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 items-end">
        <Field label="kind">
          <Select value={m.kind} onChange={e => setM({...m, kind: e.target.value})}>
            <option>person</option><option>vehicle</option><option>face</option>
          </Select>
        </Field>
        <Field label="path"><Input value={m.path} onChange={e => setM({...m, path: e.target.value})} /></Field>
        <Field label="precision">
          <Select value={m.precision} onChange={e => setM({...m, precision: e.target.value})}>
            <option>FP16</option><option>FP32</option><option>INT8</option>
          </Select>
        </Field>
        <Button onClick={create}>{t("common.add")}</Button>
      </div>
      <Table>
        <thead><tr><Th>kind</Th><Th>path</Th><Th>precision</Th><Th>status</Th><Th></Th></tr></thead>
        <tbody>
          {(q.data ?? []).map(m => (
            <Tr key={m.id}>
              <Td>{m.kind}</Td>
              <Td className="font-mono text-xs truncate max-w-xs">{m.path}</Td>
              <Td>{m.precision}</Td>
              <Td>{m.enabled ? "on" : "off"}</Td>
              <Td className="text-right">
                <Button size="sm" variant="ghost" onClick={() => toggle(m.id, m.enabled)}>{m.enabled ? "disable" : "enable"}</Button>
              </Td>
            </Tr>
          ))}
        </tbody>
      </Table>
    </CardBody></Card>
  );
}

function Faces() {
  const { t } = useTranslation();
  const q = useQuery({ queryKey: ["faces"], queryFn: () => api<FacePerson[]>("/api/v1/ai/faces").catch(() => []) });
  const [name, setName] = useState("");
  const [file, setFile] = useState<File | null>(null);

  const enroll = async () => {
    if (!file || !name) return;
    const fd = new FormData(); fd.append("name", name); fd.append("image", file);
    try { await fetch("/api/v1/ai/faces/enroll", { method: "POST", body: fd, headers: { "Authorization": `Bearer ${localStorage.getItem("nvr.token") || ""}` }});
      toast("ok", "success"); q.refetch(); }
    catch (e: any) { toast(e.message, "danger"); }
  };

  return (
    <Card><CardBody className="space-y-3">
      <div className="grid grid-cols-2 md:grid-cols-3 gap-2 items-end">
        <Field label="name"><Input value={name} onChange={e => setName(e.target.value)} /></Field>
        <Field label="image"><Input type="file" onChange={e => setFile(e.target.files?.[0] ?? null)} /></Field>
        <Button onClick={enroll}>{t("ai.enroll")}</Button>
      </div>
      <Table>
        <thead><tr><Th>id</Th><Th>name</Th></tr></thead>
        <tbody>
          {(q.data ?? []).map(p => <Tr key={p.id}><Td>{p.id}</Td><Td>{p.name}</Td></Tr>)}
        </tbody>
      </Table>
    </CardBody></Card>
  );
}

function Search() {
  const { t } = useTranslation();
  const cams = useQuery<Camera[]>({ queryKey: ["cameras"], queryFn: Cameras.list });
  const [obj, setObj] = useState("person");
  const [cam, setCam] = useState("");
  const [from, setFrom] = useState(""); const [to, setTo] = useState("");
  const [results, setResults] = useState<any[]>([]);
  const run = async () => {
    const u = new URLSearchParams({ object: obj });
    if (cam)  u.set("camera_id", cam);
    if (from) u.set("from", new Date(from).toISOString());
    if (to)   u.set("to",   new Date(to).toISOString());
    try {
      setResults(await Events.searchDetections(u));
    } catch (e: unknown) {
      toast(toastMessageForApiError(e, t), "danger");
    }
  };
  return (
    <Card><CardBody className="space-y-3">
      <div className="grid grid-cols-2 md:grid-cols-5 gap-2 items-end">
        <Field label={t("ai.object")}>
          <Select value={obj} onChange={e => setObj(e.target.value)}>
            <option value="motion">motion</option>
            <option value="onvif">onvif (все)</option>
            <option value="person">person</option>
            <option value="vehicle">vehicle</option>
            <option value="face">face</option>
          </Select>
        </Field>
        <Field label={t("archive.camera")}>
          <Select value={cam} onChange={e => setCam(e.target.value)}>
            <option value="">{t("common.all")}</option>
            {(cams.data ?? []).map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
          </Select>
        </Field>
        <Field label="from"><Input type="datetime-local" value={from} onChange={e => setFrom(e.target.value)} /></Field>
        <Field label="to">  <Input type="datetime-local" value={to}   onChange={e => setTo(e.target.value)}   /></Field>
        <Button onClick={run}>{t("common.search")}</Button>
      </div>
      <Table>
        <thead><tr><Th>ts</Th><Th>camera</Th><Th>type</Th><Th>conf</Th></tr></thead>
        <tbody>
          {results.map((r, i) => (
            <Tr key={i}>
              <Td className="font-mono text-xs">{r.ts}</Td>
              <Td>{r.camera_id}</Td>
              <Td>{r.type}</Td>
              <Td>{r.confidence?.toFixed(2)}</Td>
            </Tr>
          ))}
        </tbody>
      </Table>
    </CardBody></Card>
  );
}
