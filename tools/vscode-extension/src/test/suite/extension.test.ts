import * as assert from 'assert';
import * as path from 'path';
import * as vscode from 'vscode';

// Real, automated verification against the real VS Code API -- not a unit
// test of buildLineIndex()/parseCallSite() in isolation. This exercises the
// full path: a real chronicle-cli serve instance (started before this test
// run, serving examples/export/demo.chronicle) -> the extension's actual
// HTTP fetch -> the actual CodeLensProvider registered with VS Code's real
// language-feature pipeline -> vscode.executeCodeLensProvider, the same
// command VS Code's own CodeLens rendering uses internally.
suite('Chronicle History Extension', () => {
    test('CodeLenses appear on the real call-site lines from demo.chronicle', async function () {
        this.timeout(20000);

        // No workspace folder is opened via CLI args (see runTest.ts's
        // comment on the VS Code 1.129.1 / test-electron incompatibility
        // that forced this) -- set the server URL directly instead of
        // relying on examples/export/.vscode/settings.json.
        await vscode.workspace
            .getConfiguration('chronicle')
            .update('serverUrl', 'http://127.0.0.1:8199', vscode.ConfigurationTarget.Global);

        const mainCppPath = path.resolve(__dirname, '../../../../../examples/export/main.cpp');
        const document = await vscode.workspace.openTextDocument(mainCppPath);
        await vscode.window.showTextDocument(document);

        // The extension refreshes on activation and every
        // chronicle.refreshIntervalSeconds (3s in this workspace's
        // .vscode/settings.json) -- give it a real round-trip to the
        // running server rather than asserting immediately.
        await new Promise((resolve) => setTimeout(resolve, 4000));

        const lenses = await vscode.commands.executeCommand<vscode.CodeLens[]>(
            'vscode.executeCodeLensProvider',
            document.uri
        );

        assert.ok(lenses, 'expected CodeLens results, got none');
        assert.ok(lenses!.length > 0, `expected at least one CodeLens, got ${lenses!.length}`);

        // track(health, ...) at line 20 (1-based) -> 0-based line 19.
        // Only one event (version 0) has a known call site for
        // player.health, since plain `health = 75` assignment can't
        // capture one (ADR 0010) -- so this line's CodeLens must report
        // exactly 1 change.
        const healthLens = lenses!.find((l) => l.range.start.line === 19);
        assert.ok(healthLens, 'expected a CodeLens on line 20 (player.health\'s track() call)');
        assert.match(healthLens!.command!.title, /1 change/);
        assert.match(healthLens!.command!.title, /player\.health/);

        // push_back(...) calls at lines 27/28/29 and update() at line 30
        // (1-based) all capture their own call site (named methods, unlike
        // operator=) -- each of those lines should have its own CodeLens.
        const inventoryLensLines = lenses!
            .filter((l) => l.command?.title.includes('player.inventory'))
            .map((l) => l.range.start.line + 1); // back to 1-based for a readable assertion
        assert.deepStrictEqual(
            inventoryLensLines.sort((a, b) => a - b),
            [27, 28, 29, 30],
            `expected player.inventory CodeLenses on lines 27-30, got ${JSON.stringify(inventoryLensLines)}`
        );
    });
});
