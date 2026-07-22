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
        // Originally set to target Insiders on the theory that Stable's
        // single-instance lock was intercepting new launches. That theory
        // is disproven (see docs/adr/0022-vscode-extension.md): a fresh,
        // never-before-run Insiders build -- no competing instance to
        // forward to -- failed identically. The real cause (established
        // by eleven independently-varied invocation strategies, all
        // converging on the same Chromium-bootstrap-level crash) appears
        // to be that this tool's execution context has no interactive
        // window station attached, which blocks any Electron GUI launch
        // from here regardless of channel. Left targeting Insiders anyway
        // since it's a reasonable default for whichever environment
        // (CI, an interactive desktop session) eventually runs this.
        version: 'insiders',
        extensionDevelopmentPath,
        extensionTestsPath,
        launchArgs: ['--disable-extensions'],
    });
}

main().catch((err) => {
    console.error('Failed to run tests:', err);
    process.exit(1);
});
