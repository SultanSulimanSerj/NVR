import { ButtonHTMLAttributes, forwardRef } from "react";

type Variant = "primary" | "secondary" | "danger" | "ghost";
type Size    = "sm" | "md" | "lg";

interface Props extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: Variant;
  size?:    Size;
}

const variantCls: Record<Variant, string> = {
  primary:   "bg-indigo-600 hover:bg-indigo-500 text-white disabled:bg-indigo-900 disabled:text-neutral-400",
  secondary: "bg-neutral-800 hover:bg-neutral-700 text-neutral-100 border border-neutral-700",
  danger:    "bg-red-600 hover:bg-red-500 text-white",
  ghost:     "hover:bg-neutral-800 text-neutral-300",
};
const sizeCls: Record<Size, string> = {
  sm: "px-2 py-1 text-xs",
  md: "px-3 py-1.5 text-sm",
  lg: "px-4 py-2 text-base",
};

export const Button = forwardRef<HTMLButtonElement, Props>(
  ({ variant = "primary", size = "md", className = "", ...p }, ref) => (
    <button ref={ref}
      className={`rounded inline-flex items-center justify-center gap-1.5 transition disabled:opacity-50 ${variantCls[variant]} ${sizeCls[size]} ${className}`}
      {...p} />
  ));
Button.displayName = "Button";
