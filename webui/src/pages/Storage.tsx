import { useEffect, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Archive, Storage, Config, useAuth } from "../lib/api";
import { Card, CardBody, CardTitle, Button, Field, Input, toast, Textarea } from "../components/ui";

function fmtBytes(n: number) {
  if (n <= 0) return "—";
  const gib = n / (1024 * 1024 * 1024);
  if (gib >= 1) return `${gib.toFixed(2)} GiB`;
  return `${(n / (1024 * 1024)).toFixed(1)} MiB`;
}

export function StoragePage() {
  const { t } = useTranslation();
  const role = useAuth(s => s.role);
  const disks = useQuery<any>({ queryKey: ["disks"], queryFn: Storage.disks });
  const usage = useQuery({ queryKey: ["usage"], queryFn: Storage.usage, refetchInterval: 10000 });
  const arch  = useQuery({ queryKey: ["archive-cfg"], queryFn: Config.archiveGet });
  const rootsHealth = useQuery({
    queryKey: ["archive-roots-health"],
    queryFn: Archive.rootsHealth,
    enabled: role === "admin" || role === "operator",
    refetchInterval: 60000,
    retry: false,
  });
  const [a, setA] = useState(arch.data ?? null);
  useEffect(() => {
    if (!arch.data) return;
    setA({
      ...arch.data,
      extra_archive_roots: arch.data.extra_archive_roots ?? [],
      export_watermark_text: arch.data.export_watermark_text ?? "",
    });
  }, [arch.data]);
  const [smartDev, setSmartDev] = useState<string>("");
  const smart = useQuery<any>({ queryKey: ["smart", smartDev], queryFn: () => Storage.smart(smartDev), enabled: !!smartDev });

  const save = async () => {
    if (!a) return;
    try { await Config.archivePut(a); toast(t("common.saved"), "success"); }
    catch (e: any) { toast(e.message, "danger"); }
  };

  const rootsText = (a?.extra_archive_roots ?? []).join("\n");
  const setRootsText = (text: string) => {
    if (!a) return;
    const lines = text.split(/\r?\n/).map(s => s.trim()).filter(Boolean);
    setA({ ...a, extra_archive_roots: lines });
  };

  const flat: any[] = [];
  function walk(n: any) {
    if (!n) return;
    if (Array.isArray(n)) return n.forEach(walk);
    flat.push(n);
    if (n.children) walk(n.children);
  }
  walk(disks.data?.blockdevices);
  const usedPct = usage.data ? Math.round(usage.data.archive_used_ratio * 100) : 0;

  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.storage")}</h1>

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("storage.quota")}</CardTitle></div>
        <CardBody>
          {a && (
            <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
              <Field label="root_path"><Input value={a.root_path} onChange={e => setA({...a, root_path: e.target.value})} /></Field>
              <Field label="segment_minutes"><Input type="number" value={a.segment_minutes} onChange={e => setA({...a, segment_minutes: Number(e.target.value)})} /></Field>
              <Field label="target_usage_ratio (0-1)"><Input type="number" step="0.01" value={a.target_usage_ratio} onChange={e => setA({...a, target_usage_ratio: Number(e.target.value)})} /></Field>
              <Field label="release_to_ratio"><Input type="number" step="0.01" value={a.release_to_ratio} onChange={e => setA({...a, release_to_ratio: Number(e.target.value)})} /></Field>
              <Field label="min_keep_minutes"><Input type="number" value={a.min_keep_minutes} onChange={e => setA({...a, min_keep_minutes: Number(e.target.value)})} /></Field>
              <Field label="file_prefix"><Input value={a.file_prefix ?? ""} onChange={e => setA({...a, file_prefix: e.target.value})} /></Field>
              <Field label="file_extension"><Input value={a.file_extension ?? ""} onChange={e => setA({...a, file_extension: e.target.value})} /></Field>
              <div className="col-span-2 space-y-1">
                <Field label="export_watermark_text">
                  <Input value={a.export_watermark_text ?? ""} onChange={e => setA({...a, export_watermark_text: e.target.value})} />
                </Field>
              </div>
              <div className="col-span-2 space-y-1">
                <Field label="extra_archive_roots (по строке на путь)">
                  <Textarea rows={4} className="text-xs" value={rootsText} onChange={e => setRootsText(e.target.value)} />
                </Field>
              </div>
              <div className="self-end"><Button onClick={save}>{t("common.save")}</Button></div>
            </div>
          )}
        </CardBody>
      </Card>

      {(role === "admin" || role === "operator") && (
        <Card>
          <div className="px-4 py-3 border-b border-neutral-800">
            <CardTitle>{t("storage.roots_health")}</CardTitle>
          </div>
          <CardBody className="space-y-2 text-sm">
            {rootsHealth.isLoading && <div className="text-neutral-500">{t("common.loading")}</div>}
            {rootsHealth.isError && (
              <div className="text-amber-600 text-xs">{(rootsHealth.error as Error)?.message ?? "—"}</div>
            )}
            {rootsHealth.data?.roots?.map((r, i) => (
              <div
                key={`${r.path}-${i}`}
                className="flex flex-wrap items-baseline justify-between gap-2 border-b border-neutral-800/40 py-2 last:border-0"
              >
                <div className="font-mono text-xs break-all min-w-0 flex-1">{r.path}</div>
                <div className="text-xs text-neutral-400 shrink-0 text-right space-x-2">
                  <span>{r.exists ? t("storage.root_exists_yes") : t("storage.root_exists_no")}</span>
                  <span>{r.writable ? t("storage.root_writable_yes") : t("storage.root_writable_no")}</span>
                  <span>
                    {t("storage.root_free")}: {fmtBytes(r.free_bytes)} / {fmtBytes(r.capacity_bytes)}
                  </span>
                </div>
              </div>
            ))}
          </CardBody>
        </Card>
      )}

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("storage.archive_usage")}</CardTitle></div>
        <CardBody>
          <div className="bg-neutral-800 h-4 rounded overflow-hidden">
            <div className="bg-indigo-600 h-full" style={{ width: `${usedPct}%` }} />
          </div>
          <div className="text-xs text-neutral-400 mt-1">{usedPct}%</div>
          {usage.data && <pre className="text-xs text-neutral-400 mt-3 overflow-auto">{usage.data.df}</pre>}
        </CardBody>
      </Card>

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("storage.disks")}</CardTitle></div>
        <CardBody className="space-y-2">
          {flat.filter(d => d.type === "disk").map((d, i) => (
            <div key={i} className="flex items-center justify-between text-sm border-b border-neutral-800/40 py-1">
              <div>
                <span className="font-mono">{d.name}</span>
                <span className="ml-2 text-neutral-500">{d.size} {d.model}</span>
              </div>
              <Button size="sm" variant="ghost" onClick={() => setSmartDev(d.name)}>SMART</Button>
            </div>
          ))}
          {smartDev && (
            <div className="bg-neutral-950 rounded p-2 text-xs">
              <div className="flex justify-between mb-1">
                <span className="font-mono">{smartDev}</span>
                <button onClick={() => setSmartDev("")} className="text-neutral-500">close</button>
              </div>
              <pre className="overflow-auto max-h-72">{JSON.stringify(smart.data, null, 2)}</pre>
            </div>
          )}
        </CardBody>
      </Card>
    </div>
  );
}
