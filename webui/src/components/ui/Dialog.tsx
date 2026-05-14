import { PropsWithChildren, useEffect, useRef } from "react";

interface Props {
  open: boolean;
  onClose: () => void;
  title?: string;
  width?: string;
}

// Tab order inside dialogs. We restrict the focus ring with a simple
// focus-trap so keyboard users can't escape the modal context.
const FOCUSABLE =
  'a[href], area[href], button:not([disabled]), textarea:not([disabled]), ' +
  'input:not([disabled]):not([type="hidden"]), select:not([disabled]), ' +
  '[tabindex]:not([tabindex="-1"])';

export function Dialog({ open, onClose, title, width = "max-w-lg", children }: PropsWithChildren<Props>) {
  const containerRef = useRef<HTMLDivElement>(null);
  const returnFocusRef = useRef<HTMLElement | null>(null);

  useEffect(() => {
    if (!open) return;

    // Remember where focus was so we can restore it on close.
    returnFocusRef.current = document.activeElement as HTMLElement | null;

    // Move focus into the dialog on next tick.
    requestAnimationFrame(() => {
      const c = containerRef.current;
      if (!c) return;
      const first = c.querySelector<HTMLElement>(FOCUSABLE);
      (first ?? c).focus();
    });

    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") { e.preventDefault(); onClose(); return; }
      if (e.key !== "Tab")    return;
      const c = containerRef.current;
      if (!c) return;
      const items = Array.from(c.querySelectorAll<HTMLElement>(FOCUSABLE))
        .filter(el => !el.hasAttribute("disabled"));
      if (items.length === 0) { e.preventDefault(); return; }
      const first = items[0];
      const last  = items[items.length - 1];
      const active = document.activeElement as HTMLElement | null;
      if (e.shiftKey && active === first) {
        e.preventDefault(); last.focus();
      } else if (!e.shiftKey && active === last) {
        e.preventDefault(); first.focus();
      }
    };

    window.addEventListener("keydown", onKey);
    const prevOverflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";

    return () => {
      window.removeEventListener("keydown", onKey);
      document.body.style.overflow = prevOverflow;
      const r = returnFocusRef.current;
      if (r && typeof r.focus === "function") r.focus();
    };
  }, [open, onClose]);

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-50 bg-black/70 flex items-center justify-center p-4"
         onClick={onClose} role="presentation">
      <div ref={containerRef}
           role="dialog" aria-modal="true" aria-label={title ?? "dialog"}
           tabIndex={-1}
           className={`w-full ${width} bg-neutral-900 border border-neutral-800 rounded-xl shadow-xl focus:outline-none`}
           onClick={(e) => e.stopPropagation()}>
        {title && (
          <div className="px-5 py-3 border-b border-neutral-800 flex items-center justify-between">
            <h2 className="text-lg font-semibold">{title}</h2>
            <button onClick={onClose} aria-label="close"
                    className="text-neutral-500 hover:text-neutral-200">x</button>
          </div>
        )}
        <div className="p-5">{children}</div>
      </div>
    </div>
  );
}
