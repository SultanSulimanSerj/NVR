import { useState, useMemo } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Notifications, Channel, Rule, System, Config, useAuth, Cameras } from "../lib/api";
import { Button, Card, CardBody, CardTitle, Dialog, Field, Input, Select, Textarea, Tabs, toast } from "../components/ui";

const KIND_FIELDS: Record<string, string[]> = {
  email:    ["smtp_host", "smtp_port", "smtp_user", "smtp_pass", "from", "to"],
  telegram: ["bot_token", "chat_id"],
  webhook:  ["url", "headers_json"],
  mqtt:     ["broker_host", "broker_port", "topic", "user", "pass"],
};

export function NotifyPage() {
  const { t } = useTranslation();
  const role = useAuth(s => s.role);
  const qc = useQueryClient();
  const channels = useQuery({ queryKey: ["notifications", "channels"], queryFn: Notifications.channels });
  const rules    = useQuery({ queryKey: ["notifications", "rules"], queryFn: Notifications.rules });
  const sysInfo  = useQuery({ queryKey: ["system", "info"], queryFn: System.info });
  const features = useQuery({
    queryKey: ["config", "features"],
    queryFn: Config.featuresGet,
    enabled: role === "admin",
  });
  const setMqttLive = async (live: boolean) => {
    try {
      await Config.featuresPut(live ? "live" : "stub");
      toast("ok", "success");
      qc.invalidateQueries({ queryKey: ["system", "info"] });
      qc.invalidateQueries({ queryKey: ["config", "features"] });
    } catch (e: any) {
      toast(e.message, "danger");
    }
  };
  const kindFields = useMemo(() => {
    const kf: Record<string, string[]> = { ...KIND_FIELDS };
    const mqttLive =
      sysInfo.data?.features?.mqtt === true && sysInfo.data?.features?.mqtt_delivery !== "stub";
    if (!mqttLive) delete kf.mqtt;
    return kf;
  }, [sysInfo.data]);
  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.notifications")}</h1>
      {role === "admin" && (
        <Card>
          <CardBody className="flex flex-wrap items-center gap-3 text-sm">
            <span>MQTT delivery: <code>{features.data?.mqtt_delivery ?? sysInfo.data?.features?.mqtt_delivery ?? "?"}</code></span>
            <Button size="sm" variant="secondary" onClick={() => setMqttLive(false)}>stub</Button>
            <Button size="sm" onClick={() => setMqttLive(true)}>live</Button>
          </CardBody>
        </Card>
      )}
      <Tabs tabs={[
        { id: "ch", label: t("notify.channels"), content: <ChannelList items={channels.data ?? []} kindFields={kindFields} /> },
        { id: "rl", label: t("notify.rules"),    content: <RuleList items={rules.data ?? []} /> },
      ]} />
    </div>
  );
}

function ChannelList({ items, kindFields }: { items: Channel[]; kindFields: Record<string, string[]> }) {
  const { t } = useTranslation();
  const qc = useQueryClient();
  const [edit, setEdit] = useState<{ id?: number; kind: string; name: string; config: Record<string, string>; enabled: boolean } | null>(null);

  const create = () => setEdit({ kind: "email", name: "Email 1", config: {}, enabled: true });

  // Pull stored config (with masked secrets) so user can patch a single field
  // without losing the SMTP password etc.
  const openEdit = async (c: Channel) => {
    try {
      const full = await Notifications.channel(c.id);
      const cfg: Record<string, string> = {};
      const raw = (full.config as Record<string, unknown>) || {};
      for (const [k, v] of Object.entries(raw)) {
        cfg[k] = v === null || v === undefined ? "" : String(v);
      }
      setEdit({ id: c.id, kind: c.kind, name: full.name, config: cfg, enabled: full.enabled });
    } catch {
      setEdit({ id: c.id, kind: c.kind, name: c.name, config: {}, enabled: c.enabled });
    }
  };

  const save = async () => {
    if (!edit) return;
    try {
      if (edit.id) {
        // Drop masked secret placeholders so we don't pretend to update them.
        const payload: Record<string, string> = {};
        for (const [k, v] of Object.entries(edit.config)) {
          if (v === "********") continue;
          payload[k] = v;
        }
        await Notifications.patchChannel(edit.id,
          { name: edit.name, enabled: edit.enabled, config: payload });
      } else {
        await Notifications.createChannel(edit.kind, edit.name, edit.config);
      }
      toast(t("common.saved"), "success"); setEdit(null);
      qc.invalidateQueries({ queryKey: ["notifications", "channels"] });
    } catch (e: any) { toast(e.message, "danger"); }
  };
  const remove = async (id: number) => {
    if (!confirm(t("common.confirm_delete") ?? "Delete?")) return;
    await Notifications.removeChannel(id);
    qc.invalidateQueries({ queryKey: ["notifications", "channels"] });
  };
  const test = async (id: number) => {
    try { await Notifications.testChannel(id); toast("queued", "success"); }
    catch (e: any) { toast(e.message, "danger"); }
  };

  return (
    <Card>
      <div className="px-4 py-3 border-b border-neutral-800 flex justify-between">
        <CardTitle>{t("notify.channels")}</CardTitle>
        <Button onClick={create} size="sm">{t("common.add")}</Button>
      </div>
      <CardBody className="space-y-1">
        {items.map(c => (
          <div key={c.id} className="flex items-center gap-2 py-1 border-b border-neutral-800/40">
            <span className="font-mono text-xs">{c.kind}</span>
            <span>{c.name}</span>
            <span className={`text-xs ${c.enabled ? "text-emerald-400" : "text-neutral-500"}`}>{c.enabled ? "on" : "off"}</span>
            <div className="ml-auto space-x-1">
              <Button size="sm" variant="ghost" onClick={() => test(c.id)}>{t("notify.test")}</Button>
              <Button size="sm" variant="ghost" onClick={() => openEdit(c)}>{t("common.edit")}</Button>
              <Button size="sm" variant="ghost" onClick={() => remove(c.id)}>{t("common.delete")}</Button>
            </div>
          </div>
        ))}

        <Dialog open={!!edit} onClose={() => setEdit(null)} title={t("notify.channel")}>
          {edit && (
            <div className="grid grid-cols-2 gap-3">
              <Field label="kind">
                <Select value={edit.kind} onChange={e => setEdit({...edit, kind: e.target.value, config: {}})} disabled={!!edit.id}>
                  {Object.keys(kindFields).map(k => <option key={k}>{k}</option>)}
                </Select>
              </Field>
              <Field label="name"><Input value={edit.name} onChange={e => setEdit({...edit, name: e.target.value})} /></Field>
              {(kindFields[edit.kind] ?? []).map(f => (
                <Field key={f} label={f}>
                  <Input type={f.includes("pass") || f.includes("token") ? "password" : "text"}
                          value={edit.config[f] ?? ""}
                          onChange={e => setEdit({...edit, config: {...edit.config, [f]: e.target.value}})} />
                </Field>
              ))}
              <label className="flex items-center gap-2 col-span-2">
                <input type="checkbox" checked={edit.enabled} onChange={e => setEdit({...edit, enabled: e.target.checked})} />enabled
              </label>
              <div className="col-span-2 flex justify-end gap-2">
                <Button variant="secondary" onClick={() => setEdit(null)}>{t("common.cancel")}</Button>
                <Button onClick={save}>{t("common.save")}</Button>
              </div>
            </div>
          )}
        </Dialog>
      </CardBody>
    </Card>
  );
}

function RuleList({ items }: { items: Rule[] }) {
  const { t } = useTranslation();
  const qc = useQueryClient();
  const cams = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list });
  const chs  = useQuery({ queryKey: ["notifications", "channels"], queryFn: Notifications.channels });
  const [edit, setEdit] = useState<Partial<Rule> | null>(null);

  const save = async () => {
    if (!edit) return;
    try { await Notifications.createRule(edit); toast(t("common.saved"), "success"); setEdit(null);
      qc.invalidateQueries({ queryKey: ["notifications", "rules"] }); }
    catch (e: any) { toast(e.message, "danger"); }
  };
  const remove = async (id: number) => {
    if (!confirm(t("common.confirm_delete") ?? "Delete?")) return;
    await Notifications.removeRule(id);
    qc.invalidateQueries({ queryKey: ["notifications", "rules"] });
  };

  return (
    <Card>
      <div className="px-4 py-3 border-b border-neutral-800 flex justify-between">
        <CardTitle>{t("notify.rules")}</CardTitle>
        <Button onClick={() => setEdit({ event_type: "*", severity_min: "info", throttle_seconds: 30, channel_id: chs.data?.[0]?.id })} size="sm">{t("common.add")}</Button>
      </div>
      <CardBody className="space-y-1">
        {items.map(r => {
          const ch = chs.data?.find(c => c.id === r.channel_id);
          return (
            <div key={r.id} className="flex gap-2 items-center py-1 border-b border-neutral-800/40 text-sm">
              <span className="font-mono text-xs">{r.camera_id || "*"}</span>
              <span>{r.event_type}</span>
              <span className="text-neutral-500 text-xs">{">="}{r.severity_min}</span>
              <span>{ch?.name ?? `ch:${r.channel_id}`}</span>
              <span className="text-neutral-500 text-xs">{r.throttle_seconds}s</span>
              <Button className="ml-auto" size="sm" variant="ghost" onClick={() => remove(r.id)}>{t("common.delete")}</Button>
            </div>
          );
        })}

        <Dialog open={!!edit} onClose={() => setEdit(null)} title={t("notify.rule")}>
          {edit && (
            <div className="grid grid-cols-2 gap-3">
              <Field label="camera">
                <Select value={edit.camera_id ?? ""} onChange={e => setEdit({...edit, camera_id: e.target.value})}>
                  <option value="">{t("common.all")}</option>
                  {(cams.data ?? []).map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
                </Select>
              </Field>
              <Field label="event_type"><Input value={edit.event_type ?? ""} onChange={e => setEdit({...edit, event_type: e.target.value})} /></Field>
              <Field label="severity_min">
                <Select value={edit.severity_min ?? "info"} onChange={e => setEdit({...edit, severity_min: e.target.value})}>
                  <option>info</option><option>warning</option><option>critical</option>
                </Select>
              </Field>
              <Field label="throttle_seconds"><Input type="number" value={edit.throttle_seconds ?? 30} onChange={e => setEdit({...edit, throttle_seconds: Number(e.target.value)})} /></Field>
              <Field label="channel">
                <Select value={edit.channel_id ?? 0} onChange={e => setEdit({...edit, channel_id: Number(e.target.value)})}>
                  {(chs.data ?? []).map(c => <option key={c.id} value={c.id}>{c.name} ({c.kind})</option>)}
                </Select>
              </Field>
              <div className="col-span-2 flex justify-end gap-2">
                <Button variant="secondary" onClick={() => setEdit(null)}>{t("common.cancel")}</Button>
                <Button onClick={save}>{t("common.save")}</Button>
              </div>
            </div>
          )}
        </Dialog>
      </CardBody>
    </Card>
  );
}
