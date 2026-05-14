import { useMemo, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Audit } from "../lib/api";
import { Card, Input, Field, Table, Th, Td, Tr, Badge } from "../components/ui";

export function AuditPage() {
  const { t } = useTranslation();
  const [user, setUser]   = useState("");
  const [action, setAction] = useState("");
  const q = useMemo(() => {
    const u = new URLSearchParams({ limit: "300" });
    if (user)   u.set("user", user);
    if (action) u.set("action", action);
    return u;
  }, [user, action]);
  const log = useQuery({ queryKey: ["audit", q.toString()], queryFn: () => Audit.list(q) });
  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.audit")}</h1>
      <Card className="p-3">
        <div className="grid grid-cols-2 md:grid-cols-3 gap-2">
          <Field label="user"><Input value={user} onChange={e => setUser(e.target.value)} /></Field>
          <Field label="action"><Input value={action} onChange={e => setAction(e.target.value)} /></Field>
        </div>
      </Card>
      <Card>
        <Table>
          <thead><tr><Th>ts</Th><Th>user</Th><Th>action</Th><Th>target</Th><Th>payload</Th></tr></thead>
          <tbody>
            {(log.data ?? []).map(r => (
              <Tr key={r.id}>
                <Td className="font-mono text-xs">{r.ts.replace("T", " ").substring(0,19)}</Td>
                <Td>{r.user}</Td>
                <Td><Badge>{r.action}</Badge></Td>
                <Td className="font-mono text-xs">{r.target}</Td>
                <Td className="text-xs text-neutral-400 max-w-md truncate">{r.payload}</Td>
              </Tr>
            ))}
          </tbody>
        </Table>
      </Card>
    </div>
  );
}
