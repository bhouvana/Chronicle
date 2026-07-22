// docs/adr/0022-vscode-extension.md (docs/10-roadmap.md's v2.0 item):
// "inline gutter annotations ('this field changed N times in this
// session, click to see history'), reusing the browser viewer's webview."
//
// Implemented as a CodeLens, not a raw gutter decoration: VS Code's
// TextEditorDecorationType has no click handler at all (checked directly
// against the API, not assumed) -- CodeLens is the actual, correct
// mechanism for "a count annotated on a line that you can click," the
// same one VS Code's own built-in "N references" annotations use.
//
// Zero new C++ code: this extension is a pure consumer of chronicle-cli
// serve's existing /api/session endpoint (docs/adr/0016-interactive-
// browser-viewer.md) -- the exact same JSON shape session_to_json()
// already produces, including per-event `callSite` ("filename:line",
// only present when known() -- ADR 0010's call-site capture). Grouping
// events by call site is what turns that data into "this line has N
// recorded mutations."

import * as vscode from 'vscode';
import * as http from 'http';
import * as https from 'https';

interface ChronicleEvent {
    version: number;
    elapsed_ns: number;
    value: unknown;
    callSite?: string; // "filename:line", only present when known()
}

interface ChronicleStream {
    name: string;
    shape: string;
    events: ChronicleEvent[];
}

interface ChronicleSession {
    streams: ChronicleStream[];
}

// filename -> line -> [{streamName, count}]
type LineIndex = Map<string, Map<number, { streamName: string; count: number }[]>>;

function fetchJson(url: string): Promise<ChronicleSession> {
    return new Promise((resolve, reject) => {
        const client = url.startsWith('https:') ? https : http;
        client
            .get(url, (res) => {
                if (res.statusCode !== 200) {
                    reject(new Error(`chronicle-cli serve returned HTTP ${res.statusCode}`));
                    res.resume();
                    return;
                }
                let body = '';
                res.on('data', (chunk) => (body += chunk));
                res.on('end', () => {
                    try {
                        resolve(JSON.parse(body) as ChronicleSession);
                    } catch (err) {
                        reject(err);
                    }
                });
            })
            .on('error', reject);
    });
}

// Matches html_export.cpp's session_to_json(): callSite is "filename:line"
// where filename is already just the basename (that C++ code strips the
// directory itself before serializing), not a full path -- this parser
// mirrors that assumption rather than re-deriving it.
function parseCallSite(callSite: string): { file: string; line: number } | null {
    const idx = callSite.lastIndexOf(':');
    if (idx < 0) {
        return null;
    }
    const line = Number(callSite.slice(idx + 1));
    if (!Number.isFinite(line) || line <= 0) {
        return null;
    }
    return { file: callSite.slice(0, idx), line };
}

function buildLineIndex(session: ChronicleSession): LineIndex {
    const index: LineIndex = new Map();
    for (const stream of session.streams) {
        for (const event of stream.events) {
            if (!event.callSite) {
                continue;
            }
            const parsed = parseCallSite(event.callSite);
            if (!parsed) {
                continue;
            }
            if (!index.has(parsed.file)) {
                index.set(parsed.file, new Map());
            }
            const perFile = index.get(parsed.file)!;
            if (!perFile.has(parsed.line)) {
                perFile.set(parsed.line, []);
            }
            const entries = perFile.get(parsed.line)!;
            const existing = entries.find((e) => e.streamName === stream.name);
            if (existing) {
                existing.count += 1;
            } else {
                entries.push({ streamName: stream.name, count: 1 });
            }
        }
    }
    return index;
}

class ChronicleCodeLensProvider implements vscode.CodeLensProvider {
    private lineIndex: LineIndex = new Map();
    private readonly _onDidChangeCodeLenses = new vscode.EventEmitter<void>();
    readonly onDidChangeCodeLenses = this._onDidChangeCodeLenses.event;

    setLineIndex(index: LineIndex): void {
        this.lineIndex = index;
        this._onDidChangeCodeLenses.fire();
    }

    provideCodeLenses(document: vscode.TextDocument): vscode.CodeLens[] {
        const basename = document.fileName.split(/[\\/]/).pop() ?? document.fileName;
        const perFile = this.lineIndex.get(basename);
        if (!perFile) {
            return [];
        }
        const lenses: vscode.CodeLens[] = [];
        for (const [line, entries] of perFile) {
            const lineIndex0 = line - 1; // callSite lines are 1-based; VS Code Positions are 0-based
            if (lineIndex0 < 0 || lineIndex0 >= document.lineCount) {
                continue;
            }
            const totalChanges = entries.reduce((sum, e) => sum + e.count, 0);
            const streamList = entries.map((e) => `${e.streamName} (${e.count})`).join(', ');
            const range = new vscode.Range(lineIndex0, 0, lineIndex0, 0);
            lenses.push(
                new vscode.CodeLens(range, {
                    title: `$(history) ${totalChanges} change${totalChanges === 1 ? '' : 's'} recorded — ${streamList}`,
                    command: 'chronicle.showHistory',
                    arguments: [],
                })
            );
        }
        return lenses;
    }
}

export function activate(context: vscode.ExtensionContext): void {
    const provider = new ChronicleCodeLensProvider();
    context.subscriptions.push(
        vscode.languages.registerCodeLensProvider([{ language: 'cpp' }, { language: 'c' }], provider)
    );

    let panel: vscode.WebviewPanel | undefined;

    async function refresh(): Promise<void> {
        const serverUrl = vscode.workspace.getConfiguration('chronicle').get<string>('serverUrl', 'http://127.0.0.1:8080');
        try {
            const session = await fetchJson(`${serverUrl}/api/session`);
            provider.setLineIndex(buildLineIndex(session));
        } catch (err) {
            // Cold-path failure (server not running / not reachable) is not
            // an error state worth interrupting the user over -- CodeLenses
            // just stay empty until the next successful refresh, the same
            // "fail quietly, don't block the editor" posture the extension
            // needs for a tool that's often not running.
            void err;
        }
    }

    context.subscriptions.push(
        vscode.commands.registerCommand('chronicle.refresh', refresh),
        vscode.commands.registerCommand('chronicle.showHistory', () => {
            const serverUrl = vscode.workspace
                .getConfiguration('chronicle')
                .get<string>('serverUrl', 'http://127.0.0.1:8080');
            if (panel) {
                panel.reveal(vscode.ViewColumn.Beside);
            } else {
                panel = vscode.window.createWebviewPanel(
                    'chronicleHistory',
                    'Chronicle History',
                    vscode.ViewColumn.Beside,
                    { enableScripts: true, retainContextWhenHidden: true }
                );
                panel.onDidDispose(() => {
                    panel = undefined;
                });
            }
            // Reuses the exact live-server page chronicle-cli serve already
            // produces (docs/adr/0016) via an iframe -- this *is* "reusing
            // the browser viewer's webview," not a re-implementation of it.
            panel.webview.html = `<!doctype html><html><body style="margin:0;padding:0;">
                <iframe src="${serverUrl}/" style="width:100%;height:100vh;border:none;"></iframe>
            </body></html>`;
        })
    );

    const intervalSeconds = vscode.workspace.getConfiguration('chronicle').get<number>('refreshIntervalSeconds', 5);
    const timer = setInterval(refresh, Math.max(1, intervalSeconds) * 1000);
    context.subscriptions.push({ dispose: () => clearInterval(timer) });

    void refresh();
}

export function deactivate(): void {
    // Nothing to clean up beyond what context.subscriptions already handles.
}
