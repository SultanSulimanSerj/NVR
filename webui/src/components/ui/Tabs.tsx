import { ReactNode, useState } from "react";

interface Tab { id: string; label: string; content: ReactNode; }

export function Tabs({ tabs, initial }: { tabs: Tab[]; initial?: string }) {
  const [active, setActive] = useState(initial || tabs[0]?.id);
  const cur = tabs.find(t => t.id === active);
  return (
    <div>
      <div className="flex gap-1 border-b border-neutral-800 mb-4">
        {tabs.map(t => (
          <button key={t.id} onClick={() => setActive(t.id)}
            className={`px-3 py-2 text-sm border-b-2 transition ${
              active === t.id
                ? "border-indigo-500 text-white"
                : "border-transparent text-neutral-400 hover:text-neutral-200"
            }`}>{t.label}</button>
        ))}
      </div>
      <div>{cur?.content}</div>
    </div>
  );
}
