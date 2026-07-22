// Pure-logic verification, runnable with plain Node (no Electron, no VS
// Code API) -- exists because @vscode/test-electron could not be driven
// end-to-end in this environment: no Electron GUI process launched from
// this tool's execution context (by any of eleven independently-varied
// strategies -- see docs/adr/0022-vscode-extension.md's "Verification
// performed, and its real limits" section for the full diagnosis) can
// get past Chromium's earliest bootstrap phase, most likely because
// nothing launched from here has access to an interactive window
// station. Documented honestly in that ADR rather than claimed as full
// UI-level verification this environment could not actually produce.
// See also src/test/providerIntegration.test.ts, which goes further:
// it runs extension.ts's real activate() and provideCodeLenses() against
// a vscode API shim, not just this test's internal helper functions.
//
// This test exercises the exact same buildLineIndex()/parseCallSite()
// logic extension.ts uses, against the real JSON a running chronicle-cli
// serve instance returns for examples/export/demo.chronicle -- the actual
// data transformation the CodeLensProvider depends on, verified for real.

import * as assert from 'assert';
import * as http from 'http';

interface ChronicleEvent {
    version: number;
    elapsed_ns: number;
    value: unknown;
    callSite?: string;
}
interface ChronicleStream {
    name: string;
    shape: string;
    events: ChronicleEvent[];
}
interface ChronicleSession {
    streams: ChronicleStream[];
}
type LineIndex = Map<string, Map<number, { streamName: string; count: number }[]>>;

function parseCallSite(callSite: string): { file: string; line: number } | null {
    const idx = callSite.lastIndexOf(':');
    if (idx < 0) return null;
    const line = Number(callSite.slice(idx + 1));
    if (!Number.isFinite(line) || line <= 0) return null;
    return { file: callSite.slice(0, idx), line };
}

function buildLineIndex(session: ChronicleSession): LineIndex {
    const index: LineIndex = new Map();
    for (const stream of session.streams) {
        for (const event of stream.events) {
            if (!event.callSite) continue;
            const parsed = parseCallSite(event.callSite);
            if (!parsed) continue;
            if (!index.has(parsed.file)) index.set(parsed.file, new Map());
            const perFile = index.get(parsed.file)!;
            if (!perFile.has(parsed.line)) perFile.set(parsed.line, []);
            const entries = perFile.get(parsed.line)!;
            const existing = entries.find((e) => e.streamName === stream.name);
            if (existing) existing.count += 1;
            else entries.push({ streamName: stream.name, count: 1 });
        }
    }
    return index;
}

function fetchJson(url: string): Promise<ChronicleSession> {
    return new Promise((resolve, reject) => {
        http
            .get(url, (res) => {
                let body = '';
                res.on('data', (c) => (body += c));
                res.on('end', () => {
                    try {
                        resolve(JSON.parse(body));
                    } catch (e) {
                        reject(e);
                    }
                });
            })
            .on('error', reject);
    });
}

async function main(): Promise<void> {
    const serverUrl = process.argv[2] ?? 'http://127.0.0.1:8199';
    const session = await fetchJson(`${serverUrl}/api/session`);
    const index = buildLineIndex(session);

    const perFile = index.get('main.cpp');
    assert.ok(perFile, 'expected main.cpp entries in the line index');

    // player.health: only track()'s own call (version 0) has a known call
    // site (line 20) -- plain `health = 75` assignment cannot capture one
    // (ADR 0010), so line 20 must show exactly 1 change for that stream.
    const line20 = perFile!.get(20);
    assert.ok(line20, 'expected an entry for line 20 (player.health track())');
    const healthEntry = line20!.find((e) => e.streamName === 'player.health');
    assert.ok(healthEntry, 'expected player.health at line 20');
    assert.strictEqual(healthEntry!.count, 1, `expected exactly 1 change at line 20, got ${healthEntry!.count}`);

    // player.inventory: push_back()/update() are named methods that DO
    // capture their own call site -- lines 27/28/29/30 should each have
    // their own single-event entry.
    for (const line of [27, 28, 29, 30]) {
        const entries: { streamName: string; count: number }[] | undefined = perFile!.get(line);
        assert.ok(entries, `expected an entry at line ${line} for player.inventory`);
        const inv: { streamName: string; count: number } | undefined = entries!.find(
            (e) => e.streamName === 'player.inventory'
        );
        assert.ok(inv, `expected player.inventory at line ${line}`);
        assert.strictEqual(inv!.count, 1, `expected exactly 1 change at line ${line}, got ${inv!.count}`);
    }

    console.log('PASS: buildLineIndex()/parseCallSite() produce the exact expected line->count mapping');
}

main().catch((err) => {
    console.error('FAIL:', err);
    process.exit(1);
});
