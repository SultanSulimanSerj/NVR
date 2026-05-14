import { InputHTMLAttributes, SelectHTMLAttributes, TextareaHTMLAttributes, forwardRef } from "react";

const base = "w-full px-2.5 py-1.5 rounded bg-neutral-800 border border-neutral-700 " +
             "text-neutral-100 placeholder-neutral-500 focus:outline-none focus:border-indigo-500 " +
             "disabled:opacity-50 text-sm";

export const Input = forwardRef<HTMLInputElement, InputHTMLAttributes<HTMLInputElement>>(
  ({ className = "", ...p }, ref) => <input ref={ref} className={`${base} ${className}`} {...p} />);
Input.displayName = "Input";

export const Select = forwardRef<HTMLSelectElement, SelectHTMLAttributes<HTMLSelectElement>>(
  ({ className = "", children, ...p }, ref) => (
    <select ref={ref} className={`${base} ${className}`} {...p}>{children}</select>
  ));
Select.displayName = "Select";

export const Textarea = forwardRef<HTMLTextAreaElement, TextareaHTMLAttributes<HTMLTextAreaElement>>(
  ({ className = "", ...p }, ref) => (
    <textarea ref={ref} className={`${base} font-mono ${className}`} {...p} />
  ));
Textarea.displayName = "Textarea";

export const Label = ({ children }: { children: React.ReactNode }) => (
  <label className="block text-xs text-neutral-400 mb-1">{children}</label>
);

export const Field = ({ label, children }: { label: string; children: React.ReactNode }) => (
  <div className="space-y-1">
    <Label>{label}</Label>
    {children}
  </div>
);
