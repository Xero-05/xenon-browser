param([Parameter(Mandatory=$true)][string]$Zip)
$ErrorActionPreference = 'Stop'
$taskZip = [IO.Path]::GetFullPath($Zip)
$taskExpected = (Get-Content -LiteralPath ($taskZip + '.sha256') -Raw).Split(' ')[0].Trim()
if ((Get-FileHash -LiteralPath $taskZip -Algorithm SHA256).Hash -ne $taskExpected) { throw 'ZIP checksum mismatch.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskArchive = [IO.Compression.ZipFile]::OpenRead($taskZip)
try {
  $taskPrefix = [IO.Path]::GetFileNameWithoutExtension($taskZip) + '/'
  $taskEntries = @{}
  foreach ($entry in $taskArchive.Entries) {
    if (-not $entry.FullName.StartsWith($taskPrefix) -or $entry.FullName -match '(^|/)\.\.(/|$)|\\|:') { throw 'Unexpected archive path.' }
    if ($taskEntries.ContainsKey($entry.FullName)) { throw 'Duplicate archive entry.' }
    if ($entry.FullName -match 'XenonAuthTest|vault_fixture_seed|auth-key|broker-state|vault\.sqlite|client-config|\.pdb$|\.lib$') { throw 'A development or private file entered the runtime package.' }
    $taskEntries[$entry.FullName] = $entry
  }
  $taskManifestEntry = $taskEntries[$taskPrefix + 'release-manifest.json']
  if (-not $taskManifestEntry) { throw 'Release manifest missing.' }
  $taskReader = [IO.StreamReader]::new($taskManifestEntry.Open())
  try { $taskManifest = $taskReader.ReadToEnd() | ConvertFrom-Json } finally { $taskReader.Dispose() }
  if ($taskManifest.signed -ne $false) { throw 'Alpha must be labelled unsigned.' }
  $taskVersionEntry = $taskEntries[$taskPrefix + 'VERSION']
  if (-not $taskVersionEntry) { throw 'Release VERSION is missing.' }
  $taskVersionReader = [IO.StreamReader]::new($taskVersionEntry.Open())
  try { $taskVersionText = $taskVersionReader.ReadToEnd().Trim() } finally { $taskVersionReader.Dispose() }
  if ($taskVersionText -cne $taskManifest.version -or $taskVersionText -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-alpha\.[0-9]+)?$') { throw 'Release manifest and VERSION disagree.' }
  if (-not $taskEntries.ContainsKey($taskPrefix + 'licenses/INNO-SETUP-LICENSE.txt')) { throw 'Installer runtime license is missing.' }
  $taskSeen = @{}
  foreach ($item in $taskManifest.files) {
    $name = $taskPrefix + $item.path
    $entry = $taskEntries[$name]
    if (-not $entry -or $entry.Length -ne $item.bytes -or $taskSeen.ContainsKey($name)) { throw "Invalid manifest item: $($item.path)" }
    $taskSeen[$name] = $true
    $stream = $entry.Open(); $hasher = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose(); $stream.Dispose() }
    if ($hash -ne $item.sha256) { throw "File checksum mismatch: $($item.path)" }
  }
  foreach ($name in $taskEntries.Keys) {
    if ($name.EndsWith('/') -or $name -eq ($taskPrefix + 'release-manifest.json')) { continue }
    if (-not $taskSeen.ContainsKey($name)) { throw 'An unlisted file entered the runtime package.' }
  }
  foreach ($required in @('Xenon.exe','Xenon.dll','libcef.dll','runtime/node.exe','runtime/NODE-LICENSE.txt','adapter/dist/src/cli.js','LICENSE','NOTICE','THIRD_PARTY_NOTICES.md','SECURITY.md','AGENTS.md','docs/GETTING_STARTED.md','docs/USER_GUIDE.md','docs/AGENT_GUIDE.md','CEF-LICENSE.txt','Chromium-CREDITS.html','licenses/nlohmann-json-MIT.txt','licenses/CEF-LICENSE.txt','licenses/NODE-LICENSE.txt','licenses/MCP-SDK-LICENSE.txt','licenses/ZOD-LICENSE.txt','node_modules/@modelcontextprotocol/core/LICENSE','node_modules/@modelcontextprotocol/server/LICENSE','node_modules/zod/LICENSE')) {
    if (-not $taskSeen.ContainsKey($taskPrefix + $required)) { throw "Required runtime file missing: $required" }
  }
  Write-Host "Verified ZIP checksum and $($taskManifest.files.Count) runtime file hashes; no test binaries or private state."
} finally { $taskArchive.Dispose() }
