import { useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";
import { Setup, Onvif, Cameras } from "../lib/api";
import { Button, Card, CardBody, Field, Input, Select, toast } from "../components/ui";

type Step = "license" | "token" | "host" | "admin" | "archive" | "scan" | "done";

interface OnvifEntry {
  endpoint: string;
  xaddrs:   string;
  types:    string;
  scopes:   string;
  selected: boolean;
}

export function SetupWizard() {
  const nav = useNavigate();
  const [step, setStep]             = useState<Step>("license");
  const [hostname, setHostname]     = useState("nvr");
  const [tz, setTz]                 = useState("UTC");
  const [adminPass, setAdminPass]   = useState("");
  const [adminPass2, setAdminPass2] = useState("");
  const [archivePath, setArchive]   = useState("/var/lib/nvr-prototype/archive");
  const [setupToken, setSetupToken] = useState("");
  const [discovered, setDisc]       = useState<OnvifEntry[]>([]);
  const [scanning, setScanning]     = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const [checked, setChecked]       = useState(false);

  // Guard: if /api/v1/setup/status reports first_run=false, the wizard is
  // re-runnable only via the recovery flow. Send the user back to /login so
  // they don't accidentally overwrite an existing admin.
  useEffect(() => {
    Setup.status()
      .then(s => {
        if (!s.first_run) {
          toast("Setup already completed", "info");
          nav("/login", { replace: true });
        }
      })
      .catch(() => { /* ignore — backend may be coming up */ })
      .finally(() => setChecked(true));
  }, [nav]);

  // Password strength: ≥ 8 chars and at least 3 of {lower, upper, digit, symbol}.
  function pwScore(p: string): number {
    if (p.length < 8) return 0;
    let s = 0;
    if (/[a-z]/.test(p)) s++;
    if (/[A-Z]/.test(p)) s++;
    if (/[0-9]/.test(p)) s++;
    if (/[^A-Za-z0-9]/.test(p)) s++;
    return s;
  }

  const finalize = async () => {
    if (submitting) return;
    setSubmitting(true);
    try {
      await Setup.finalize(
        { hostname, tz, admin_password: adminPass, archive_root: archivePath },
        setupToken.trim()
      );

      const failures: string[] = [];
      for (const d of discovered.filter(x => x.selected)) {
        const host = (d.xaddrs.match(/https?:\/\/([^:/]+)/) ?? [])[1];
        if (!host) continue;
        try {
          await Cameras.create({
            id: host.replace(/\./g, "_"),
            name: host,
            rtsp_url: `rtsp://${host}/stream1`,
            onvif_host: host, onvif_port: 80,
            enable_motion: true, enable_recording: true, enable_substream: true,
          });
        } catch (e: any) {
          failures.push(`${host}: ${e?.message ?? "failed"}`);
        }
      }
      if (failures.length) {
        toast("Some cameras failed to import — see Cameras page", "danger");
      }
      nav("/", { replace: true });
    } catch (e: any) {
      toast(e?.message ?? "finalize failed", "danger");
    } finally {
      setSubmitting(false);
    }
  };

  const scan = async () => {
    setScanning(true);
    try {
      const r = await Onvif.discover();
      setDisc(r.map(x => ({ ...x, selected: true })));
    }
    catch (e: any) { toast(e?.message ?? "discover failed", "danger"); }
    finally { setScanning(false); }
  };

  const Stepper = () => {
    const steps: Step[] = ["license", "token", "host", "admin", "archive", "scan", "done"];
    const idx = steps.indexOf(step);
    return (
      <div className="flex gap-1 text-[10px] mb-4 flex-wrap">
        {steps.map((s, i) => (
          <div key={s}
               className={`px-2 py-1 rounded ${i <= idx ? "bg-indigo-600 text-white" : "bg-neutral-800 text-neutral-400"}`}>
            {s}
          </div>
        ))}
      </div>
    );
  };

  if (!checked) {
    return (
      <div className="min-h-screen flex items-center justify-center bg-neutral-950">
        <div className="text-neutral-500 animate-pulse">Loading…</div>
      </div>
    );
  }

  const score = pwScore(adminPass);

  return (
    <div className="min-h-screen flex items-center justify-center p-6 bg-neutral-950">
      <Card className="w-full max-w-2xl">
        <CardBody className="space-y-4">
          <h1 className="text-2xl font-bold">NVR — Setup</h1>
          <Stepper />

          {step === "license" && (
            <div className="space-y-3">
              <p className="text-sm text-neutral-300">
                Прототип NVR на базе FFmpeg/OpenCV/pybind11. Используя ПО, вы соглашаетесь
                с условиями (см. LICENSE).
              </p>
              <Button onClick={() => setStep("token")}>Принимаю</Button>
            </div>
          )}

          {step === "token" && (
            <div className="space-y-3">
              <p className="text-sm text-neutral-300">
                Введите одноразовый setup token. Он создан установщиком и записан в файл
                <code className="mx-1 px-1 bg-neutral-800 rounded">/etc/nvr-prototype/setup.token</code>
                на appliance (получите его через консоль/SSH у администратора).
              </p>
              <Field label="Setup token">
                <Input value={setupToken} onChange={e => setSetupToken(e.target.value)}
                       placeholder="hex token" autoComplete="off" spellCheck={false} />
              </Field>
              <div className="flex gap-2">
                <Button variant="secondary" onClick={() => setStep("license")}>Назад</Button>
                <Button disabled={setupToken.trim().length < 16} onClick={() => setStep("host")}>Далее</Button>
              </div>
            </div>
          )}

          {step === "host" && (
            <div className="space-y-3">
              <Field label="Hostname">
                <Input value={hostname} onChange={e => setHostname(e.target.value)}
                       pattern="[A-Za-z0-9-]+" maxLength={63} />
              </Field>
              <Field label="Timezone">
                <Select value={tz} onChange={e => setTz(e.target.value)}>
                  <option>UTC</option><option>Europe/Moscow</option><option>Europe/London</option>
                  <option>Europe/Berlin</option><option>America/New_York</option><option>Asia/Tokyo</option>
                </Select>
              </Field>
              <div className="flex gap-2">
                <Button variant="secondary" onClick={() => setStep("token")}>Назад</Button>
                <Button onClick={() => setStep("admin")}>Далее</Button>
              </div>
            </div>
          )}

          {step === "admin" && (
            <div className="space-y-3">
              <Field label="Логин: admin. Введите пароль (≥ 8 символов)">
                <Input type="password" value={adminPass}
                       onChange={e => setAdminPass(e.target.value)}
                       autoComplete="new-password" />
              </Field>
              <Field label="Повторите пароль">
                <Input type="password" value={adminPass2}
                       onChange={e => setAdminPass2(e.target.value)}
                       autoComplete="new-password" />
              </Field>
              {adminPass && (
                <div className="text-xs text-neutral-400">
                  Сила пароля:&nbsp;
                  <span className={
                    score === 0 ? "text-red-400" :
                    score <= 2 ? "text-yellow-400" :
                    "text-green-400"}>
                    {score === 0 ? "слабый" : score <= 2 ? "средний" : "хороший"}
                  </span>
                </div>
              )}
              {adminPass && adminPass !== adminPass2 &&
                <div className="text-xs text-red-400">Пароли не совпадают</div>}
              <div className="flex gap-2">
                <Button variant="secondary" onClick={() => setStep("host")}>Назад</Button>
                <Button disabled={score < 2 || adminPass !== adminPass2}
                         onClick={() => setStep("archive")}>Далее</Button>
              </div>
            </div>
          )}

          {step === "archive" && (
            <div className="space-y-3">
              <Field label="Путь архива">
                <Input value={archivePath} onChange={e => setArchive(e.target.value)} />
              </Field>
              <div className="flex gap-2">
                <Button variant="secondary" onClick={() => setStep("admin")}>Назад</Button>
                <Button onClick={() => setStep("scan")}>Далее</Button>
              </div>
            </div>
          )}

          {step === "scan" && (
            <div className="space-y-3">
              <Button variant="secondary" onClick={scan} disabled={scanning}>
                {scanning ? "Поиск…" : "Сканировать ONVIF"}
              </Button>
              <ul className="space-y-1 max-h-60 overflow-auto">
                {discovered.map((d, i) => (
                  <li key={i} className="p-2 bg-neutral-800 rounded text-sm flex items-center gap-2">
                    <input type="checkbox" checked={d.selected}
                            onChange={e => setDisc(discovered.map((x, j) =>
                              j === i ? { ...x, selected: e.target.checked } : x))} />
                    <span className="font-mono text-xs flex-1 truncate">{d.xaddrs}</span>
                  </li>
                ))}
              </ul>
              <div className="flex gap-2">
                <Button variant="secondary" onClick={() => setStep("archive")}>Назад</Button>
                <Button onClick={() => setStep("done")}>Далее</Button>
              </div>
            </div>
          )}

          {step === "done" && (
            <div className="space-y-3">
              <div className="text-sm text-neutral-300">
                Готово! Подтвердите завершение настройки. Будет создан пользователь
                <b className="mx-1">admin</b> и импортированы выбранные камеры.
              </div>
              <div className="flex gap-2">
                <Button variant="secondary" onClick={() => setStep("scan")}>Назад</Button>
                <Button onClick={finalize} disabled={submitting}>
                  {submitting ? "Завершение…" : "Завершить"}
                </Button>
              </div>
            </div>
          )}
        </CardBody>
      </Card>
    </div>
  );
}
