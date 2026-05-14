import { HTMLAttributes, TdHTMLAttributes, ThHTMLAttributes } from "react";

export const Table = ({ className = "", ...p }: HTMLAttributes<HTMLTableElement>) => (
  <table className={`w-full text-sm ${className}`} {...p} />
);
export const Th = ({ className = "", ...p }: ThHTMLAttributes<HTMLTableCellElement>) => (
  <th className={`text-left text-[11px] uppercase tracking-wider text-neutral-500 font-medium px-3 py-2 border-b border-neutral-800 ${className}`} {...p} />
);
export const Td = ({ className = "", ...p }: TdHTMLAttributes<HTMLTableCellElement>) => (
  <td className={`px-3 py-2 border-b border-neutral-800/50 ${className}`} {...p} />
);
export const Tr = ({ className = "", ...p }: HTMLAttributes<HTMLTableRowElement>) => (
  <tr className={`hover:bg-neutral-900/40 ${className}`} {...p} />
);
