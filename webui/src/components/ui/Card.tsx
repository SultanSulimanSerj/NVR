import { HTMLAttributes } from "react";

export const Card = ({ className = "", ...p }: HTMLAttributes<HTMLDivElement>) => (
  <div className={`bg-neutral-900 border border-neutral-800 rounded-lg ${className}`} {...p} />
);

export const CardHeader = ({ className = "", ...p }: HTMLAttributes<HTMLDivElement>) => (
  <div className={`px-4 py-3 border-b border-neutral-800 ${className}`} {...p} />
);

export const CardBody = ({ className = "", ...p }: HTMLAttributes<HTMLDivElement>) => (
  <div className={`px-4 py-3 ${className}`} {...p} />
);

export const CardTitle = ({ className = "", ...p }: HTMLAttributes<HTMLHeadingElement>) => (
  <h3 className={`text-base font-semibold text-neutral-100 ${className}`} {...p} />
);
