# Third-party notices

Xenon source is Apache-2.0. This license does not replace the licenses of its dependencies.

| Component | Pinned version | License and provenance |
| --- | --- | --- |
| Chromium Embedded Framework | 154.0.32+g682c378, Chromium 154.0.8037.58 | BSD-style; [CEF project](https://github.com/chromiumembedded/cef), [license copy](third_party/licenses/CEF-LICENSE.txt). Copyright Marshall A. Greenblatt; portions copyright Google Inc. Runtime includes `CEF-LICENSE.txt`. |
| Chromium and bundled libraries | Matching CEF distribution | BSD-style Chromium license and multiple third-party licenses; [Chromium project](https://www.chromium.org/), [source](https://chromium.googlesource.com/chromium/src/). Runtime includes the complete `Chromium-CREDITS.html` supplied by CEF, including copyright notices and license texts. |
| SQLite | 3.53.4 | [Public domain](https://www.sqlite.org/copyright.html); official amalgamation. |
| nlohmann/json | 3.12.0 | [MIT](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT); copyright Niels Lohmann. |
| Node.js | 24.16.0 | MIT and bundled dependency licenses; [Node.js](https://nodejs.org/), [complete license copy](third_party/licenses/NODE-LICENSE.txt). The runtime includes `runtime/NODE-LICENSE.txt`. |
| MCP TypeScript SDK | 2.2.0 | [Official SDK](https://github.com/modelcontextprotocol/typescript-sdk). The distributed [license text](third_party/licenses/MCP-SDK-LICENSE.txt) describes an MIT to Apache-2.0 transition: new code/specification contributions use Apache-2.0, older contributions without relicensing consent retain MIT, and documentation contributions excluding specifications use CC-BY-4.0. Runtime packages are `@modelcontextprotocol/server` and `@modelcontextprotocol/core`; `client` is development-only. Package metadata still says MIT; retain the full upstream text. |
| Zod | 4.6.5 | [Zod](https://github.com/colinhacks/zod), [MIT license copy](third_party/licenses/ZOD-LICENSE.txt); copyright Colin McDonnell. |
| TypeScript | 5.9.3 | [TypeScript](https://github.com/microsoft/TypeScript), Apache-2.0 with upstream third-party notices; build-only dependency. |
| Node TypeScript declarations | `@types/node` 24.10.1; `undici-types` pinned in npm lockfile | MIT; development-only packages from [DefinitelyTyped](https://github.com/DefinitelyTyped/DefinitelyTyped) and [Undici](https://github.com/nodejs/undici). License files accompany the installed packages. |

Production npm packages, their metadata and upstream license files are retained in the release's `node_modules/`. See `npm-dependencies.json` in the release for the exact runtime dependency inventory and `package-lock.json` for integrity digests. Test/development dependencies are not shipped in the runtime ZIP.

The source tree retains selected upstream license texts under `third_party/licenses/`; the release copies that directory to `licenses/`. When reading this document inside an extracted release, use that directory for the license-copy links above. The complete Chromium credits and Node license cover their bundled dependencies, not just the headline projects. Keep these notices and all accompanying license files when redistributing Xenon. Dependency license terms remain applicable; Xenon's Apache-2.0 license does not relicense upstream code.

Xenon modifies the copied CEF bootstrap's icon/version resources for branding. It does not modify its executable code, manifest or sandbox entry point. See [branding provenance and verification](docs/BRANDING.md). The Xenon artwork is supplied for this project; its provenance is documented in [assets/branding/README.md](assets/branding/README.md).

The architecture draws on CEF's Chrome runtime and request contexts, the official MCP SDK's transport compatibility, and browser automation's practice of combining accessibility evidence with visual evidence. These references do not imply endorsement by their maintainers. Chromium trademarks and third-party product names remain the property of their owners.
