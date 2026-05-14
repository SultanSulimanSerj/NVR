import { Navigate } from "react-router-dom";
import { useQuery } from "@tanstack/react-query";
import { Auth, Role, useAuth } from "../lib/api";

const rank: Record<Role, number> = { viewer: 1, operator: 2, admin: 3 };

function normalize(role: string | null | undefined): Role | null {
  if (!role) return null;
  const r = role.toLowerCase();
  if (r === "admin" || r === "operator" || r === "viewer") return r as Role;
  return null;
}

// Two-tier check:
//   1. The role we cached in localStorage (instant; can be lying if the user
//      was disabled / demoted server-side).
//   2. A best-effort `Auth.me()` round-trip whose answer overrides the cache
//      once it lands. While we wait we still render so the navigation feels
//      snappy.
export function RequireRole({ role, children }: { role: Role; children: JSX.Element }) {
  const cached = normalize(useAuth(s => s.role));
  const setToken = useAuth(s => s.setToken);

  const me = useQuery({
    queryKey: ["auth", "me"],
    queryFn: Auth.me,
    staleTime: 30_000,
    retry: false,
  });

  if (!cached) return <Navigate to="/login" replace />;

  const server = normalize(me.data?.role as string | undefined) ?? cached;
  if (server !== cached) {
    setToken(useAuth.getState().token, server, useAuth.getState().login);
  }

  if (rank[server] < rank[role]) {
    return (
      <div className="p-8 text-center text-neutral-400">
        Недостаточно прав. Требуется роль <b>{role}</b>.
      </div>
    );
  }
  return children;
}
