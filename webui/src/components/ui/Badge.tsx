import { HTMLAttributes } from "react";

type Tone = "default" | "info" | "warn" | "danger" | "success";
const cls: Record<Tone, string> = {
  default: "bg-neutral-700 text-neutral-200",
  info:    "bg-blue-700 text-blue-100",
  warn:    "bg-amber-700 text-amber-100",
  danger:  "bg-red-700 text-red-100",
  success: "bg-emerald-700 text-emerald-100",
};

export function Badge({ tone = "default", className = "", ...p }: { tone?: Tone } & HTMLAttributes<HTMLSpanElement>) {
  return <span className={`inline-block px-1.5 py-0.5 text-[10px] rounded ${cls[tone]} ${className}`} {...p} />;
}
