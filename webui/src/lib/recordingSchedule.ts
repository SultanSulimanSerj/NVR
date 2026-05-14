import type { Camera } from "./api";

export const WEEKDAY_KEYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"] as const;
export type Weekday = (typeof WEEKDAY_KEYS)[number];

export type ScheduleWindow = { start: string; end: string };

/** Form state aligned with API `recording_schedule` (server normalizes JSON). */
export type RecordingScheduleForm = {
  always: boolean;
  weekdays: Record<Weekday, ScheduleWindow[]>;
};

export function emptyWeekdays(): Record<Weekday, ScheduleWindow[]> {
  return { sun: [], mon: [], tue: [], wed: [], thu: [], fri: [], sat: [] };
}

export function defaultRecordingSchedule(): RecordingScheduleForm {
  return { always: true, weekdays: emptyWeekdays() };
}

function trimHm(s: string): string {
  const t = s.trim();
  if (t.length >= 5) return t.slice(0, 5);
  return t;
}

export function scheduleFromCamera(cam: {
  recording_schedule?: Camera["recording_schedule"];
}): RecordingScheduleForm {
  const s = cam.recording_schedule;
  const base = emptyWeekdays();
  if (s && typeof s.always === "boolean") {
    const wd = s.weekdays;
    for (const k of WEEKDAY_KEYS) {
      const arr = wd?.[k];
      if (Array.isArray(arr)) {
        base[k] = arr
          .filter((w): w is ScheduleWindow =>
            !!w && typeof w.start === "string" && typeof w.end === "string")
          .map(w => ({ start: trimHm(w.start), end: trimHm(w.end) }));
      }
    }
    return { always: s.always, weekdays: base };
  }
  return defaultRecordingSchedule();
}

/** Payload for API (drops weekday lists when always-on). */
export function scheduleToPayload(form: RecordingScheduleForm): Camera["recording_schedule"] {
  if (form.always) return { always: true };
  const weekdays: Record<string, ScheduleWindow[]> = {};
  for (const k of WEEKDAY_KEYS) {
    weekdays[k] = form.weekdays[k].map(w => ({ start: trimHm(w.start), end: trimHm(w.end) }));
  }
  return { always: false, weekdays };
}
