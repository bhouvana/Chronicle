// Preload hook (node -r ./out/test/registerShim.js ...) that intercepts
// `require('vscode')` and redirects it to vscodeShim.ts's compiled
// output, so extension.ts's real code can be require()'d and run under
// plain Node without an actual VS Code/Electron host present.
/* eslint-disable @typescript-eslint/no-var-requires */
const Module = require('module');
const path = require('path');

const shimPath = path.resolve(__dirname, './vscodeShim.js');
const originalResolveFilename = Module._resolveFilename;
Module._resolveFilename = function (request: string, ...rest: unknown[]) {
    if (request === 'vscode') {
        return shimPath;
    }
    return originalResolveFilename.call(this, request, ...rest);
};
