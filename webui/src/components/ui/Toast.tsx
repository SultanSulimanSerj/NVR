import { create } from "zustand";

interface ToastItem { id: number; msg: string; tone: "info" | "success" | "danger"; }
interface S {
  items: ToastItem[];
  push: (msg: string, tone?: ToastItem["tone"]) => void;
  remove: (id: number) => void;
}

export const useToasts = create<S>((set, get) => ({
  items: [],
  push: (msg, tone = "info") => {
    const id = Date.now() + Math.random();
    set({ items: [...get().items, { id, msg, tone }] });
    setTimeout(() => get().remove(id), 4000);
  },
  remove: (id) => set({ items: get().items.filter(t => t.id !== id) }),
}));

export const toast = (msg: string, tone: ToastItem["tone"] = "info") =>
  useToasts.getState().push(msg, tone);

export function ToastHost() {
  const items  = useToasts(s => s.items);
  const remove = useToasts(s => s.remove);
  return (
    <div className="fixed bottom-4 right-4 z-[60] space-y-2">
      {items.map(t => (
        <div key={t.id} onClick={() => remove(t.id)}
          className={`px-3 py-2 rounded shadow-lg cursor-pointer text-sm ${
            t.tone === "success" ? "bg-emerald-700 text-white" :
            t.tone === "danger"  ? "bg-red-700 text-white" :
            "bg-neutral-800 text-neutral-100 border border-neutral-700"
          }`}>{t.msg}</div>
      ))}
    </div>
  );
}
