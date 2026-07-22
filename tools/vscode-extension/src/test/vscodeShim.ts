// A hand-rolled, minimal `vscode` module shim -- NOT a mock in the
// "stub everything, assert nothing real" sense. Each piece here mirrors
// the *actual* VS Code API shape closely enough that extension.ts's real
// compiled code (activate(), ChronicleCodeLensProvider.provideCodeLenses())
// runs completely unmodified against it under plain Node, no Electron
// involved. This exists specifically because a real Electron-hosted VS
// Code window could not be launched in this environment (see ADR 0022's
// "Verification performed, and its real limits" section for the full,
// honest diagnosis) -- this is the deepest verification achievable
// without that: real activate() wiring, a real HTTP fetch against a real
// running chronicle-cli serve instance, and the real provideCodeLenses()
// method, all exercised end to end.

export class Position {
    constructor(public readonly line: number, public readonly character: number) {}
}

export class Range {
    public readonly start: Position;
    public readonly end: Position;
    constructor(startLine: number, startChar: number, endLine: number, endChar: number) {
        this.start = new Position(startLine, startChar);
        this.end = new Position(endLine, endChar);
    }
}

export interface Command {
    title: string;
    command: string;
    arguments?: unknown[];
}

export class CodeLens {
    constructor(public readonly range: Range, public readonly command?: Command) {}
}

export class EventEmitter<T> {
    private listeners: Array<(e: T) => void> = [];
    event = (listener: (e: T) => void) => {
        this.listeners.push(listener);
        return { dispose: () => {} };
    };
    fire(e: T): void {
        for (const l of this.listeners) {
            l(e);
        }
    }
}

export const ViewColumn = { Beside: 2, Active: -1, One: 1 };

export const languages = {
    registerCodeLensProvider(_selector: unknown, provider: unknown) {
        (languages as unknown as { _lastProvider: unknown })._lastProvider = provider;
        return { dispose: () => {} };
    },
};

export const commands = {
    _registered: new Map<string, (...args: unknown[]) => unknown>(),
    registerCommand(id: string, handler: (...args: unknown[]) => unknown) {
        commands._registered.set(id, handler);
        return { dispose: () => {} };
    },
};

export const workspace = {
    _config: new Map<string, unknown>(),
    getConfiguration(_section: string) {
        return {
            get<T>(key: string, defaultValue: T): T {
                const full = `${_section}.${key}`;
                return (workspace._config.has(full) ? workspace._config.get(full) : defaultValue) as T;
            },
        };
    },
};

export const window = {
    _panels: [] as unknown[],
    createWebviewPanel(viewType: string, title: string, _showOptions: unknown, _options: unknown) {
        const panel = {
            viewType,
            title,
            webview: { html: '' },
            _disposeHandlers: [] as Array<() => void>,
            onDidDispose(handler: () => void) {
                panel._disposeHandlers.push(handler);
                return { dispose: () => {} };
            },
            reveal(_col?: unknown) {},
        };
        window._panels.push(panel);
        return panel;
    },
};
