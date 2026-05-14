import { useEffect, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import QRCode from "qrcode";
import { Users, Auth, useAuth, Role } from "../lib/api";
import { Button, Card, CardBody, CardTitle, Dialog, Field, Input, Select, Table, Th, Td, Tr, Badge, toast } from "../components/ui";

export function UsersPage() {
  const { t } = useTranslation();
  const qc = useQueryClient();
  const users = useQuery({ queryKey: ["users"], queryFn: Users.list });
  const me    = useAuth(s => s.login);

  const [adding, setAdding] = useState(false);
  const [u, setU] = useState({ login: "", password: "", role: "viewer" as Role });
  const [acl, setAcl] = useState<{ login: string; ids: string } | null>(null);

  const openAcl = async (login: string) => {
    try {
      const r = await Users.camerasGet(login);
      setAcl({ login, ids: (r.camera_ids ?? []).join(",") });
    } catch (e: any) {
      toast(e.message, "danger");
    }
  };
  const saveAcl = async () => {
    if (!acl) return;
    const ids = acl.ids.split(",").map(s => s.trim()).filter(Boolean);
    try {
      await Users.camerasPut(acl.login, ids);
      toast("ok", "success");
      setAcl(null);
    } catch (e: any) {
      toast(e.message, "danger");
    }
  };

  const create = async () => {
    try { await Users.create(u.login, u.password, u.role); toast("ok", "success"); setAdding(false);
      qc.invalidateQueries({ queryKey: ["users"] }); }
    catch (e: any) { toast(e.message, "danger"); }
  };
  const toggle = async (login: string, disabled: boolean) => {
    await Users.patch(login, { disabled: !disabled }); qc.invalidateQueries({ queryKey: ["users"] });
  };
  const resetPw = async (login: string) => {
    const np = prompt(t("users.new_password") || "new pw");
    if (!np) return;
    await Users.patch(login, { password: np }); toast("ok", "success");
  };
  const remove = async (login: string) => {
    if (!confirm(t("users.confirm_delete"))) return;
    await Users.remove(login); qc.invalidateQueries({ queryKey: ["users"] });
  };

  return (
    <div className="space-y-3">
      <div className="flex justify-between items-center">
        <h1 className="text-2xl font-semibold">{t("nav.users")}</h1>
        <Button onClick={() => setAdding(true)}>{t("users.add")}</Button>
      </div>

      <Card>
        <Table>
          <thead><tr><Th>login</Th><Th>role</Th><Th>status</Th><Th>2FA</Th><Th></Th></tr></thead>
          <tbody>
            {(users.data ?? []).map(u2 => (
              <Tr key={u2.id}>
                <Td>{u2.login} {u2.login === me && <Badge tone="info">you</Badge>}</Td>
                <Td><Badge>{u2.role}</Badge></Td>
                <Td>{u2.disabled ? <Badge tone="danger">disabled</Badge> : <Badge tone="success">active</Badge>}</Td>
                <Td>{u2.has_totp ? <Badge tone="success">enabled</Badge> : <Badge>off</Badge>}</Td>
                <Td className="space-x-1 text-right">
                  <Button size="sm" variant="ghost" onClick={() => openAcl(u2.login)}>ACL</Button>
                  <Button size="sm" variant="ghost" onClick={() => resetPw(u2.login)}>{t("users.reset_password")}</Button>
                  <Button size="sm" variant="ghost" onClick={() => toggle(u2.login, u2.disabled)}>
                    {u2.disabled ? t("users.enable") : t("users.disable")}
                  </Button>
                  <Button size="sm" variant="ghost" onClick={() => remove(u2.login)}>{t("common.delete")}</Button>
                </Td>
              </Tr>
            ))}
          </tbody>
        </Table>
      </Card>

      <MyAccount />

      <Dialog open={!!acl} onClose={() => setAcl(null)} title="Camera ACL">
        {acl && (
          <div className="space-y-3">
            <p className="text-sm text-neutral-400">User: <code>{acl.login}</code>. Comma-separated camera ids; empty = no restriction.</p>
            <Field label="camera_ids"><Input value={acl.ids} onChange={e => setAcl({ ...acl, ids: e.target.value })} placeholder="cam1,cam2" /></Field>
            <div className="flex justify-end gap-2">
              <Button variant="secondary" onClick={() => setAcl(null)}>{t("common.cancel")}</Button>
              <Button onClick={saveAcl}>{t("common.save")}</Button>
            </div>
          </div>
        )}
      </Dialog>

      <Dialog open={adding} onClose={() => setAdding(false)} title={t("users.add")}>
        <div className="grid grid-cols-2 gap-3">
          <Field label="login"><Input value={u.login} onChange={e => setU({...u, login: e.target.value})} /></Field>
          <Field label="password"><Input type="password" value={u.password} onChange={e => setU({...u, password: e.target.value})} /></Field>
          <Field label="role">
            <Select value={u.role} onChange={e => setU({...u, role: e.target.value as Role})}>
              <option value="viewer">viewer</option><option value="operator">operator</option><option value="admin">admin</option>
            </Select>
          </Field>
          <div className="col-span-2 flex justify-end gap-2">
            <Button variant="secondary" onClick={() => setAdding(false)}>{t("common.cancel")}</Button>
            <Button onClick={create}>{t("common.save")}</Button>
          </div>
        </div>
      </Dialog>
    </div>
  );
}

function MyAccount() {
  const { t } = useTranslation();
  const [old_pw, setOld] = useState("");
  const [new_pw, setNew] = useState("");
  const [totp, setTotp]  = useState<{ secret: string; otpauth_url: string } | null>(null);
  const [code, setCode]  = useState("");

  const change = async () => {
    if (new_pw.length < 8) return toast(t("users.password_too_short"), "danger");
    try { await Auth.changePassword(old_pw, new_pw); toast("ok", "success"); setOld(""); setNew(""); }
    catch (e: any) { toast(e.message, "danger"); }
  };
  const setupTotp = async () => {
    try { setTotp(await Auth.totpSetup()); } catch (e: any) { toast(e.message, "danger"); }
  };
  const verifyTotp = async () => {
    try { const r = await Auth.totpVerify(code); toast(r.ok ? "activated" : "bad code", r.ok ? "success" : "danger"); }
    catch (e: any) { toast(e.message, "danger"); }
  };

  // QR is rendered locally — no third-party request leaks the otpauth secret.
  const [qrDataUrl, setQrDataUrl] = useState<string>("");
  useEffect(() => {
    if (!totp?.otpauth_url) { setQrDataUrl(""); return; }
    let cancelled = false;
    QRCode.toDataURL(totp.otpauth_url, { margin: 1, scale: 6, errorCorrectionLevel: "M" })
      .then((u) => { if (!cancelled) setQrDataUrl(u); })
      .catch(() => { if (!cancelled) setQrDataUrl(""); });
    return () => { cancelled = true; };
  }, [totp?.otpauth_url]);

  return (
    <Card>
      <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("users.my_account")}</CardTitle></div>
      <CardBody className="space-y-4">
        <div className="grid grid-cols-2 gap-3 max-w-md">
          <Field label={t("users.old_password")}><Input type="password" value={old_pw} onChange={e => setOld(e.target.value)} /></Field>
          <Field label={t("users.new_password")}><Input type="password" value={new_pw} onChange={e => setNew(e.target.value)} /></Field>
          <div><Button onClick={change}>{t("users.change_password")}</Button></div>
        </div>
        <div>
          <h3 className="text-sm font-medium mb-2">{t("users.totp")}</h3>
          {!totp && <Button variant="secondary" onClick={setupTotp}>{t("users.totp_setup")}</Button>}
          {totp && (
            <div className="flex gap-4 items-start">
              {qrDataUrl
                ? <img src={qrDataUrl} alt="2FA QR" className="bg-white p-1 rounded" width={180} height={180} />
                : <div className="bg-neutral-900 border border-neutral-800 rounded p-3 text-xs text-neutral-400">QR…</div>}
              <div className="space-y-2">
                <div className="font-mono text-xs break-all">{totp.secret}</div>
                <Field label={t("users.totp_code")}>
                  <Input value={code} onChange={e => setCode(e.target.value)}
                         inputMode="numeric" maxLength={6} autoComplete="one-time-code" />
                </Field>
                <Button onClick={verifyTotp}>{t("users.totp_verify")}</Button>
              </div>
            </div>
          )}
        </div>
      </CardBody>
    </Card>
  );
}
