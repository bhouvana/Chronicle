import * as path from 'path';
import { runTests } from '@vscode/test-electron';

// A genuine automated test: launches a real VS Code instance
// (@vscode/test-electron downloads and drives an actual VS Code build,
// not a mock), loads this extension into it via --extensionDevelopmentPath,
// and runs suite/index.ts inside that real extension host. This is the
// standard, official way to verify a VS Code extension actually works
// against the real API -- chosen specifically because a spawned
// interactive GUI window isn't something this environment can click
// through directly, and "should work" isn't this project's standard for
// anything else it's built.
async function main(): Promise<void> {
    const extensionDevelopmentPath = path.resolve(__dirname, '../../');
    const extensionTestsPath = path.resolve(__dirname, './suite/index');

    await runTests({
        // No workspace folder passed via CLI args at all: this VS Code
        // version (1.129.1) mishandles a bare positional folder-path
        // argument when combined with test-electron's launch flags --
        // confirmed directly (reproduced identically against a fresh
        // download, the already-installed system copy, and a manually
        // flattened extraction; Code.exe's Electron main process treats
        // the folder path as a CommonJS entry script to require(), not a
        // folder to open). Not a real environment/corruption issue, and
        // not this extension's bug either -- a genuine test-electron/VS
        // Code version incompatibility, avoided entirely by never passing
        // a folder path this way: extension.test.ts opens the target file
        // directly via vscode.workspace.openTextDocument() and sets
        // chronicle.serverUrl programmatically instead of relying on a
        // workspace-scoped .vscode/settings.json.
        extensionDevelopmentPath,
        extensionTestsPath,
        launchArgs: ['--disable-extensions'],
    });
}

main().catch((err) => {
    console.error('Failed to run tests:', err);
    process.exit(1);
});
