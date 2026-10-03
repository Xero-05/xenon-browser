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

## Local scan comparison and isolated testing

Local custom scans on 2026-10-03 used Defender platform `4.18.26080.4`, signatures `1.459.530.0`, with antivirus and real-time protection enabled before and after testing. The documented `-DisableRemediation` option was used for these diagnostic scans: it scans archive contents, ignores exclusions and reports findings without applying scan remediation. It does not disable real-time protection. See Microsoft's [command-line reference](https://learn.microsoft.com/en-us/defender-endpoint/command-line-arguments-microsoft-defender-antivirus).

| Scanned artifact | Local result |
| --- | --- |
| Unchanged published alpha.12 ZIP | One threat: `Trojan:Win32/Bearfoos.A!ml`, identifying its `Xenon.exe` entry |
| Original pinned CEF `bootstrap.exe` | No threats reported |
| Current production `Xenon.dll` | No threats reported |
| Existing unshipped `XenonAuthTest.exe` | No threats reported |
| Current unshipped `XenonAuthTest.dll` | No threats reported |

The combination of these scans and the identical non-resource code narrows the observed classification difference to the branded production launcher. It does not establish the cause of the detection or a false-positive verdict. The initial scan attempt with a non-native path failed with `0x80508023` before a valid scan; the corrected Windows path succeeded. An exclusion-query attempt and a restricted-runner status query were denied access. No exclusion list is claimed to have been independently verified or changed.

The existing, separately built AuthTest target was then used against a loopback-only page in a marked disposable profile. Windows UI automation verified Enter and Go navigation against server requests, Find/Enter/Escape, gray background-tab feedback, workspace chevrons, tab and Controls selection without dashed boxes, and sidebar resizing from 240 to 290 logical pixels while retaining typed form text. The saved width was checked after native exit. Computer Use was reset immediately after the UI burst. No detected launcher was restored, renamed or executed, and no release asset was changed.

This is limited Dark-mode, 96-DPI test-target coverage. It does not clear production use, prove continuous flicker is absent, or complete Light/System, high-contrast, multiple-DPI, ownership/handoff or full menu/dialog acceptance. AuthTest contains compile-time fixture support and must never ship or be offered for ordinary browsing. Exact hashes, scan results and these limits are recorded in the [local follow-up evidence](test-results/defender-local-followup.json); the [earlier failed production verification](test-results/navigation-fixes.json) remains unchanged.

## Review status

Microsoft recommends submitting suspected incorrect detections to its [Security Intelligence submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission). A developer submission should identify the exact public executable hash, the detection name, pinned CEF version, resource-only branding process and this integrity evidence. Submit only public release artifacts; exclude profiles, pairings, vaults, credentials, raw browsing logs and personal screenshots.

The user requested a local-only investigation. No sample or report has been submitted, no Microsoft verdict has been received, and no Defender exclusion or restoration has been performed after identifying the quarantine. A release must not be described as cleared until the exact artifact has completed the relevant review and verification. The production installation hold remains in effect.
