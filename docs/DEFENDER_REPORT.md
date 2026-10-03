# Alpha.12 Defender detection investigation

On 2026-10-03, Microsoft Defender detected and quarantined the installed alpha.12 `Xenon.exe` as `Trojan:Win32/Bearfoos.A!ml`. The same detection occurred during local production bootstrap resource updates, including the resource-update temporary files. Detection events 1116 and quarantine events 1117 were observed. No conclusion that this is a false positive has been established.

Installation and production browser verification are on hold. Keep detected files quarantined. Do not add exclusions, disable protection, restore detected files, or rename the launcher to evade detection. No new release or replacement installer has been assembled from the current fixes.

## Verified artifact provenance

Read-only verification on 2026-10-03 checked the existing published ZIP and inspected its executable directly in memory, without writing or running that executable.

| Artifact | SHA-256 |
| --- | --- |
| Published alpha.12 Windows ZIP | `3b2e69c403bfb0616ff6e4def210177342712297adbd278cbb7400656ff91fde` |
| Published alpha.12 Xenon.exe | `d84b27c99384dc5461776094c808ba284aade61177f769988c2f4d76dee3d26b` |
| Pinned CEF archive | `a211b8f10a99d1db49afd5b7d9fa7ab3222ebc6ea3026b31c3c9431e7d4a5ac2` |
| Original CEF bootstrap.exe | `42151bee2597ea056289c59026b49c27f84110332a80f9abc4b0f0fbf7c380ac` |

The ZIP and executable match the recorded [publication](test-results/alpha12-publication.json) and [package](test-results/alpha12-package-smoke.json) evidence. The local CEF archive matches `dependencies.lock.json`; the bootstrap hash matches the release's previous package record. The pinned distribution is `154.0.33+ga03e714+chromium-154.0.8037.94`.

The executable comparison found 3,698,688 identical executable bytes and identical payload bytes in all eleven non-resource sections, including relocations. The sandbox manifest and six non-branding resource leaves remain unchanged. Icon/version branding grows the resource section and moves its following relocation section; relocation bytes remain identical. Resource directory code-page normalization matches the existing permitted verification rules. The pinned original bootstrap is unsigned.

These checks narrow the provenance investigation. They do not inspect every CEF/library behavior, establish publisher identity through a signature, prove the absence of compromise, or determine why Defender classified this file. The application's native policy enforcement and ordinary functional tests are also not malware analysis.

## Review status

Microsoft recommends submitting suspected incorrect detections to its [Security Intelligence submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission). A developer submission should identify the exact public executable hash, the detection name, pinned CEF version, resource-only branding process and this integrity evidence. Submit only public release artifacts; exclude profiles, pairings, vaults, credentials, raw browsing logs and personal screenshots.

No sample has been submitted, no Microsoft verdict has been received, and no Defender exclusion or restoration has been performed after identifying the quarantine. A release must not be described as cleared until the exact artifact has completed the relevant review and verification. Current UI-fix test limits are recorded [separately](test-results/navigation-fixes.json).
