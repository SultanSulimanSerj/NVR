import { Component, ErrorInfo, ReactNode } from "react";

interface Props { children: ReactNode; fallback?: (e: Error) => ReactNode }
interface State { error: Error | null }

// Catches any render-phase exception in the protected subtree so a buggy panel
// can't take the whole UI down. The fallback is plain DOM so it works even when
// the i18n / query subsystems are also broken.
export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    // eslint-disable-next-line no-console
    console.error("[ui-error]", error, info.componentStack);
  }

  reset = () => this.setState({ error: null });

  render() {
    const { error } = this.state;
    if (!error) return this.props.children;
    if (this.props.fallback) return this.props.fallback(error);
    return (
      <div className="p-6 max-w-xl">
        <div className="text-red-400 font-semibold text-lg mb-2">
          Что-то пошло не так
        </div>
        <pre className="text-xs text-neutral-400 bg-neutral-900 p-3 rounded overflow-auto">
{error.message}
        </pre>
        <div className="mt-3 flex gap-2">
          <button onClick={this.reset}
            className="px-3 py-1.5 bg-neutral-800 hover:bg-neutral-700 rounded text-sm">
            Попробовать снова
          </button>
          <button onClick={() => window.location.reload()}
            className="px-3 py-1.5 bg-indigo-700 hover:bg-indigo-600 rounded text-sm">
            Перезагрузить страницу
          </button>
        </div>
      </div>
    );
  }
}
