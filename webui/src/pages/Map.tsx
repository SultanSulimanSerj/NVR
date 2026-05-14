import { useState, useEffect } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { NavLink } from "react-router-dom";
import { useTranslation } from "react-i18next";
import { Cameras, Config, useAuth } from "../lib/api";
import { Card, CardBody, CardTitle, Button, Field, Input, toast } from "../components/ui";

export function MapPage() {
  const { t } = useTranslation();
  const qc = useQueryClient();
  const role = useAuth(s => s.role);
  const { data: cams = [] } = useQuery({ queryKey: ["cameras"], queryFn: Cameras.list });
  const mapCfg = useQuery({ queryKey: ["config", "map"], queryFn: Config.mapGet });
  const [planUrl, setPlanUrl] = useState("");
  useEffect(() => {
    if (mapCfg.data?.plan_image_url !== undefined) setPlanUrl(mapCfg.data.plan_image_url ?? "");
  }, [mapCfg.data?.plan_image_url]);

  const placed = cams.filter(
    c => typeof c.plan_x === "number" && typeof c.plan_y === "number" && c.plan_x >= 0 && c.plan_y >= 0,
  );

  const savePlan = async () => {
    try {
      await Config.mapPut(planUrl);
      toast(t("common.saved"), "success");
      void qc.invalidateQueries({ queryKey: ["config", "map"] });
    } catch (e: unknown) {
      toast((e as Error).message, "danger");
    }
  };

  const bgUrl = (mapCfg.data?.plan_image_url ?? "").trim();

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-semibold">{t("nav.map")}</h1>
        <NavLink to="/cameras" className="text-sm text-indigo-400 hover:underline">{t("nav.cameras")}</NavLink>
      </div>

      {role === "admin" && (
        <Card>
          <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("map.plan_background")}</CardTitle></div>
          <CardBody className="flex flex-wrap gap-2 items-end">
            <div className="flex-1 min-w-[200px]">
              <Field label={t("map.plan_url_label")}>
                <Input value={planUrl} onChange={e => setPlanUrl(e.target.value)} placeholder="https://…/plan.png" />
              </Field>
            </div>
            <Button onClick={() => void savePlan()}>{t("common.save")}</Button>
          </CardBody>
        </Card>
      )}

      <Card>
        <div className="px-4 py-3 border-b border-neutral-800"><CardTitle>{t("map.floor_plan")}</CardTitle></div>
        <CardBody>
          <p className="text-xs text-neutral-500 mb-3">{t("map.hint")}</p>
          <div
            className="relative w-full max-w-3xl aspect-[16/10] bg-neutral-900 rounded border border-neutral-800 overflow-hidden bg-cover bg-center"
            style={bgUrl ? { backgroundImage: `url(${bgUrl})` } : undefined}
          >
            {placed.length === 0 && (
              <div className="absolute inset-0 flex items-center justify-center text-neutral-500 text-sm bg-neutral-900/80">
                {t("map.empty")}
              </div>
            )}
            {placed.map(c => (
              <div
                key={c.id}
                className="absolute w-3 h-3 -ml-1.5 -mt-1.5 rounded-full bg-indigo-500 border border-white shadow z-10"
                style={{ left: `${c.plan_x! * 100}%`, top: `${c.plan_y! * 100}%` }}
                title={`${c.name} (${c.id})`}
              />
            ))}
          </div>
        </CardBody>
      </Card>
    </div>
  );
}
