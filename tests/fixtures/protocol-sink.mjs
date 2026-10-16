// The app a synthetic test link scheme opens: records the arguments Windows
// passed, so tests/browser-essentials-integration.ps1 can check that a link
// arrives as one escaped argument.
import fs from 'node:fs';
const [output, ...rest] = process.argv.slice(2);
fs.writeFileSync(output, JSON.stringify(rest));
