import { useState, useEffect } from "react";
import { useNavigate } from "react-router-dom";
import { useTranslation } from "react-i18next";
import { Auth, useAuth, Setup } from "../lib/api";
import { Button, Card, CardBody, Field, Input } from "../components/ui";

export function Login() {
  const { t }    = useTranslation();
  const nav      = useNavigate();
  const setToken = useAuth((s) => s.setToken);
  const [login, setLogin]       = useState("admin");
  const [password, setPassword] = useState("");
  const [totp, setTotp]         = useState("");
  const [error, setError]       = useState<string | null>(null);
  const [busy, setBusy]         = useState(false);

  useEffect(() => {
    Setup.status().then(r => { if (r.first_run) nav("/setup", { replace: true }); }).catch(() => {});
  }, [nav]);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true); setError(null);
    try {
      const r = await Auth.login(login, password, totp || undefined);
      setToken(r.token, r.role, r.login);
      nav("/", { replace: true });
    } catch {
      setError(t("login.failed"));
    } finally { setBusy(false); }
  };

  return (
    <div className="min-h-screen flex items-center justify-center bg-neutral-950">
      <Card className="w-full max-w-sm">
        <CardBody>
          <form onSubmit={submit} className="space-y-4">
            <h1 className="text-xl font-semibold">{t("login.title")}</h1>
            <Field label={t("login.user")!}><Input value={login} onChange={(e) => setLogin(e.target.value)} /></Field>
            <Field label={t("login.password")!}><Input type="password" value={password} onChange={(e) => setPassword(e.target.value)} /></Field>
            <Field label={t("login.totp_optional")!}><Input value={totp} onChange={(e) => setTotp(e.target.value)} placeholder="123456"/></Field>
            {error && <div className="text-red-400 text-sm">{error}</div>}
            <Button type="submit" disabled={busy} className="w-full">{t("login.submit")}</Button>
          </form>
        </CardBody>
      </Card>
    </div>
  );
}
