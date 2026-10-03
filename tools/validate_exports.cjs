// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
// Development-only validation; neither Node nor the validator ships with the app.
const fs = require('fs');
const path = require('path');
const validator = require(path.resolve(process.argv[3]));
(async () => {
    const directory = path.resolve(process.argv[2]);
    const files = fs.readdirSync(directory).filter(name => /\.(glb|gltf)$/.test(name));
    if (files.length < 9) throw new Error('Export contract did not produce its glTF fixtures.');
    let errors = 0;
    for (const name of files) {
        const report = await validator.validateBytes(new Uint8Array(fs.readFileSync(path.join(directory, name))), {
            uri: name,
            // Copy Buffer slices: Dart typed data must not see the surrounding Node allocation pool.
            externalResourceFunction: uri => Promise.resolve(new Uint8Array(fs.readFileSync(path.join(directory, decodeURIComponent(uri)))))
        });
        console.log(`${name}: ${report.issues.numErrors} errors, ${report.issues.numWarnings} warnings`);
        for (const issue of report.issues.messages.filter(issue => issue.severity === 0))
            console.error(`${issue.code}: ${issue.message} (${issue.pointer})`);
        errors += report.issues.numErrors;
    }
    process.exitCode = errors ? 1 : 0;
})().catch(error => { console.error(error.message); process.exitCode = 1; });
