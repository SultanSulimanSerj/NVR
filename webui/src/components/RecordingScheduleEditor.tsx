import { useTranslation } from "react-i18next";
import type { RecordingScheduleForm, ScheduleWindow, Weekday } from "../lib/recordingSchedule";
import { WEEKDAY_KEYS } from "../lib/recordingSchedule";
import { Button, Field, Input } from "./ui";

interface Props {
  value: RecordingScheduleForm;
  onChange: (next: RecordingScheduleForm) => void;
}

function cloneSchedule(s: RecordingScheduleForm): RecordingScheduleForm {
  return {
    always: s.always,
    weekdays: Object.fromEntries(
      WEEKDAY_KEYS.map(k => [k, s.weekdays[k].map(w => ({ ...w }))]),
    ) as RecordingScheduleForm["weekdays"],
  };
}

function setDay(
  s: RecordingScheduleForm,
  day: Weekday,
  windows: ScheduleWindow[],
): RecordingScheduleForm {
  const n = cloneSchedule(s);
  n.weekdays[day] = windows;
  return n;
}

export function RecordingScheduleEditor({ value, onChange }: Props) {
  const { t } = useTranslation();

  return (
    <div className="space-y-3 border border-neutral-700 rounded-lg p-3 bg-neutral-900/40">
      <div className="text-sm font-medium text-neutral-200">{t("cameras.schedule_title")}</div>
      <p className="text-xs text-neutral-500">{t("cameras.schedule_host_tz")}</p>
      <label className="flex items-center gap-2 cursor-pointer">
        <input
          type="checkbox"
          checked={value.always}
          onChange={e => {
            const always = e.target.checked;
            const w = cloneSchedule(value).weekdays;
            if (always) onChange({ always: true, weekdays: w });
            else onChange({ always: false, weekdays: w });
          }}
        />
        <span>{t("cameras.schedule_always")}</span>
      </label>

      {!value.always && (
        <div className="space-y-4 max-h-[min(50vh,420px)] overflow-y-auto pr-1">
          {WEEKDAY_KEYS.map(day => (
            <div key={day} className="border-b border-neutral-800 pb-3 last:border-0">
              <div className="text-xs font-semibold text-amber-600/90 mb-2">
                {t(`cameras.schedule_day.${day}`)}
              </div>
              <div className="space-y-2">
                {value.weekdays[day].length === 0 && (
                  <div className="text-xs text-neutral-500">{t("cameras.schedule_no_windows")}</div>
                )}
                {value.weekdays[day].map((win, idx) => (
                  <div key={idx} className="flex flex-wrap items-end gap-2">
                    <Field label={t("cameras.schedule_start")}>
                      <Input
                        type="time"
                        value={win.start}
                        onChange={e => {
                          const next = [...value.weekdays[day]];
                          next[idx] = { ...win, start: e.target.value };
                          onChange(setDay(value, day, next));
                        }}
                      />
                    </Field>
                    <Field label={t("cameras.schedule_end")}>
                      <Input
                        type="time"
                        value={win.end}
                        onChange={e => {
                          const next = [...value.weekdays[day]];
                          next[idx] = { ...win, end: e.target.value };
                          onChange(setDay(value, day, next));
                        }}
                      />
                    </Field>
                    <Button
                      type="button"
                      size="sm"
                      variant="ghost"
                      onClick={() => {
                        const next = value.weekdays[day].filter((_, i) => i !== idx);
                        onChange(setDay(value, day, next));
                      }}
                    >
                      {t("cameras.schedule_remove_window")}
                    </Button>
                  </div>
                ))}
                <Button
                  type="button"
                  size="sm"
                  variant="secondary"
                  onClick={() => {
                    onChange(setDay(value, day, [...value.weekdays[day], { start: "09:00", end: "17:00" }]));
                  }}
                >
                  {t("cameras.schedule_add_window")}
                </Button>
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
