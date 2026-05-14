import { useQuery } from "@tanstack/react-query";
import { useTranslation } from "react-i18next";
import { Notifications, NotifyDlqRow } from "../lib/api";
import { Card, CardBody, CardTitle, Table, Td, Th, Tr } from "../components/ui";

export function DlqPage() {
  const { t } = useTranslation();
  const q = useQuery({ queryKey: ["notifications", "dlq"], queryFn: () => Notifications.deadLetter(500) });

  return (
    <div className="space-y-3">
      <h1 className="text-2xl font-semibold">{t("nav.dlq")}</h1>
      <Card>
        <div className="px-4 py-3 border-b border-neutral-800">
          <CardTitle>{t("notify.dlq_title")}</CardTitle>
        </div>
        <CardBody>
          <Table>
            <thead>
              <tr>
                <Th>id</Th>
                <Th>ts</Th>
                <Th>channel</Th>
                <Th>rule</Th>
                <Th>attempts</Th>
                <Th>error</Th>
              </tr>
            </thead>
            <tbody>
              {(q.data ?? []).map((r: NotifyDlqRow) => (
                <Tr key={r.id}>
                  <Td className="font-mono text-xs">{r.id}</Td>
                  <Td className="text-xs">{r.ts}</Td>
                  <Td>{r.channel_id ?? "—"}</Td>
                  <Td>{r.rule_id ?? "—"}</Td>
                  <Td>{r.attempts}</Td>
                  <Td className="max-w-md truncate text-xs text-red-300" title={r.last_error}>{r.last_error}</Td>
                </Tr>
              ))}
            </tbody>
          </Table>
        </CardBody>
      </Card>
    </div>
  );
}
