// Runs extension.ts's *actual* compiled activate() and
// ChronicleCodeLensProvider.provideCodeLenses() under plain Node, against
// the vscodeShim.ts module (a faithful-enough stand-in for the real
// `vscode` API) and a real, running `chronicle-cli serve` instance. This
// is the deepest verification achievable in an environment where a real
// Electron-hosted VS Code window cannot be launched (see ADR 0022) --
// unlike pureLogic.test.ts (which only exercises the internal
// buildLineIndex/parseCallSite helpers), this drives extension.ts's real
// exported activate() entry point exactly as VS Code itself would call
// it, through a real HTTP fetch against real recorded C++ session data.
//
// Usage: node -r ./out/test/registerShim.js ./out/test/providerIntegration.test.js <serverUrl> <cppFilePath>

import * as path from 'path';
import * as fs from 'fs';

async function main(): Promise<void> {
    const serverUrl = process.argv[2] ?? 'http://127.0.0.1:8199';
    const cppFilePath = process.argv[3] ?? path.resolve(__dirname, '../../../../examples/export/main.cpp');

    const shim = require('vscode');
    const extension = require(path.resolve(__dirname, '../extension.js'));

    shim.workspace._config.set('chronicle.serverUrl', serverUrl);
    shim.workspace._config.set('chronicle.refreshIntervalSeconds', 3600); // don't let the timer fire during the test

    const subscriptions: Array<{ dispose(): void }> = [];
    const fakeContext = { subscriptions } as unknown;

    extension.activate(fakeContext);

    // activate() kicks off `void refresh()` (fire-and-forget async). Give
    // the real HTTP request to the real chronicle-cli serve instance time
    // to complete before asserting on its effects.
    await new Promise((resolve) => setTimeout(resolve, 1000));

    const provider = (shim.languages as { _lastProvider: unknown })._lastProvider as {
        provideCodeLenses(document: unknown): Array<{ range: { start: { line: number } }; command?: { title: string } }>;
    };
    if (!provider) {
        throw new Error('FAIL: registerCodeLensProvider was never called by activate()');
    }

    const source = fs.readFileSync(cppFilePath, 'utf8');
    const fakeDocument = {
        fileName: cppFilePath,
        lineCount: source.split(/\r\n|\r|\n/).length,
    };

    const lenses = provider.provideCodeLenses(fakeDocument);
    console.log(`provideCodeLenses() returned ${lenses.length} real CodeLens object(s):`);
    for (const lens of lenses) {
        console.log(`  line ${lens.range.start.line + 1} (0-based ${lens.range.start.line}): "${lens.command?.title}"`);
    }

    // health = 100 (line 20, track() call site) is captured as exactly 1
    // change (a bare `health = 75` assignment has no call site -- ADR
    // 0010); inventory's push_back()/update() calls on lines 27-30 and
    // scores' set() calls on lines 34-36 each show exactly 1 change --
    // the complete real mapping from examples/export/main.cpp, now
    // checked against the real vscode.CodeLens objects
    // provideCodeLenses() actually constructs and would hand to VS Code
    // (a strict superset of what pureLogic.test.ts already checked
    // against buildLineIndex() directly).
    const byLine = new Map(lenses.map((l) => [l.range.start.line + 1, l]));
    const expectedLines = [20, 27, 28, 29, 30, 34, 35, 36];
    for (const line of expectedLines) {
        const lens = byLine.get(line);
        if (!lens) {
            throw new Error(`FAIL: expected a real vscode.CodeLens at line ${line}, found none`);
        }
        if (!lens.command || !/^\$\(history\) 1 change recorded/.test(lens.command.title)) {
            throw new Error(`FAIL: line ${line} CodeLens has unexpected title: "${lens.command?.title}"`);
        }
    }
    if (lenses.length !== expectedLines.length) {
        throw new Error(`FAIL: expected exactly ${expectedLines.length} CodeLenses, got ${lenses.length}`);
    }
    console.log('PASS: activate() + real provideCodeLenses() produced the exact expected real vscode.CodeLens objects');

    // Real command-registration + webview wiring check: invoke the actual
    // registered 'chronicle.showHistory' handler (exactly what VS Code
    // does when a user clicks the CodeLens) and verify it really creates
    // a webview panel whose HTML really embeds the live server as an
    // iframe -- the "reusing the browser viewer's webview" claim from
    // ADR 0022, checked against the real handler function, not asserted.
    const showHistory = shim.commands._registered.get('chronicle.showHistory');
    if (!showHistory) {
        throw new Error('FAIL: chronicle.showHistory command was never registered by activate()');
    }
    showHistory();
    const panels = (shim.window as { _panels: Array<{ viewType: string; webview: { html: string } }> })._panels;
    if (panels.length !== 1) {
        throw new Error(`FAIL: expected exactly 1 webview panel created, got ${panels.length}`);
    }
    if (panels[0].viewType !== 'chronicleHistory') {
        throw new Error(`FAIL: unexpected webview viewType: ${panels[0].viewType}`);
    }
    if (!panels[0].webview.html.includes(`<iframe src="${serverUrl}/"`)) {
        throw new Error(`FAIL: webview HTML does not embed the live server as an iframe:\n${panels[0].webview.html}`);
    }
    console.log('PASS: real chronicle.showHistory handler created a real webview panel iframing the live server');

    for (const sub of subscriptions) {
        sub.dispose();
    }
}

main().catch((err) => {
    console.error(String(err && err.stack ? err.stack : err));
    process.exit(1);
});
