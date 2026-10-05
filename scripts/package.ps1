param([string]$Version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../VERSION') -Raw).Trim())
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[0-9A-Za-z.-]+$') { throw 'Invalid release version.' }
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Version -ne (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION') -Raw).Trim()) { throw 'Package version must match VERSION. Rebuild after changing it.' }
$taskApp = Join-Path $taskRoot 'build/app/Release'
$taskDist = Join-Path $taskRoot 'dist'
$taskName = "Xenon-$Version-windows-x64-unsigned"
$taskStage = Join-Path $taskDist ($taskName + '-stage-' + [Guid]::NewGuid().ToString('N'))
$taskPackage = Join-Path $taskStage $taskName
foreach ($required in @('Xenon.exe','Xenon.dll','libcef.dll','CEF-LICENSE.txt','Chromium-CREDITS.html')) {
  if (-not (Test-Path -LiteralPath (Join-Path $taskApp $required))) { throw "Missing build artifact: $required" }
}
$taskNumericVersion = if ($Version -match '^([0-9]+\.[0-9]+\.[0-9]+)-alpha\.([0-9]+)$') { $Matches[1] + '.' + $Matches[2] } else { $Version + '.0' }
if ([Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $taskApp 'Xenon.dll')).FileVersion -ne $taskNumericVersion) { throw 'The built browser version does not match VERSION. Rebuild before packaging.' }
if (-not (Test-Path -LiteralPath (Join-Path $taskRoot 'adapter/dist/src/cli.js'))) { throw 'Build the MCP adapter first.' }
if (-not (Test-Path -LiteralPath (Join-Path $taskRoot 'third_party/licenses/nlohmann-json-MIT.txt'))) { throw 'The checked-in nlohmann/json license is required for packaging.' }
New-Item -ItemType Directory -Force -Path $taskPackage,(Join-Path $taskPackage 'runtime'),(Join-Path $taskPackage 'adapter/dist/src') | Out-Null
# Copy runtime-only files. Never copy profiles, test binaries, archives or symbols.
$taskRuntimeNames = @('Xenon.exe','Xenon.dll','libcef.dll','chrome_elf.dll','d3dcompiler_47.dll','dxcompiler.dll','dxil.dll','icudtl.dat','chrome_100_percent.pak','chrome_200_percent.pak','resources.pak','v8_context_snapshot.bin','vk_swiftshader.dll','vk_swiftshader_icd.json','vulkan-1.dll','CEF-LICENSE.txt','Chromium-CREDITS.html')
foreach ($item in $taskRuntimeNames) { Copy-Item -LiteralPath (Join-Path $taskApp $item) -Destination $taskPackage }
Copy-Item -LiteralPath (Join-Path $taskApp 'locales') -Destination $taskPackage -Recurse
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party/node/node.exe') -Destination (Join-Path $taskPackage 'runtime')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party/node/LICENSE') -Destination (Join-Path $taskPackage 'runtime/NODE-LICENSE.txt')
Get-ChildItem -LiteralPath (Join-Path $taskRoot 'adapter/dist/src') -Filter '*.js' -File | Copy-Item -Destination (Join-Path $taskPackage 'adapter/dist/src')
foreach ($item in @('LICENSE','NOTICE','README.md','README.zh-CN.md','THIRD_PARTY_NOTICES.md','CONTRIBUTING.md','AGENTS.md','SECURITY.md','VERSION','package.json','package-lock.json','dependencies.lock.json')) { Copy-Item -LiteralPath (Join-Path $taskRoot $item) -Destination $taskPackage }
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs') -Destination $taskPackage -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $taskPackage 'assets') | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'assets/branding') -Destination (Join-Path $taskPackage 'assets/branding') -Recurse
# The extracted-archive smoke test is run after this archive is created. Do not
# bundle a previous release's result as evidence for the new package.
foreach ($taskAfterPackagingReport in @('package-smoke.json','publication.json')) {
  $taskPriorReport = Join-Path $taskPackage ('docs/test-results/' + $taskAfterPackagingReport)
  if (Test-Path -LiteralPath $taskPriorReport) { Remove-Item -LiteralPath $taskPriorReport }
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party/licenses') -Destination (Join-Path $taskPackage 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $taskRoot 'installer/INNO-SETUP-LICENSE.txt') -Destination (Join-Path $taskPackage 'licenses/INNO-SETUP-LICENSE.txt')
Push-Location $taskPackage
try {
  & npm.cmd ci --omit=dev --ignore-scripts --cache (Join-Path $taskRoot '.cache/npm')
  if ($LASTEXITCODE -ne 0) { throw 'Locked production dependency install failed.' }
  $inventory = & npm.cmd ls --omit=dev --all --json
  if ($LASTEXITCODE -ne 0) { throw 'Production dependency inventory failed.' }
  [IO.File]::WriteAllText((Join-Path $taskPackage 'npm-dependencies.json'), ($inventory -join "`n"), [Text.UTF8Encoding]::new($false))
} finally { Pop-Location }
$taskReleaseStage = if ($Version.Contains('-')) { 'local prerelease' } else { 'local release' }
$taskManifest = [ordered]@{ product='Xenon Browser';version=$Version;platform='windows-x64';signed=$false;stage=$taskReleaseStage;createdUtc=[DateTime]::UtcNow.ToString('o');sandbox='CEF matching bootstrap enabled';files=@() }
foreach ($file in Get-ChildItem -LiteralPath $taskPackage -File -Recurse) {
  $relative = $file.FullName.Substring($taskPackage.Length + 1).Replace('\','/')
  $taskManifest.files += @{ path=$relative;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
[IO.File]::WriteAllText((Join-Path $taskPackage 'release-manifest.json'), ($taskManifest | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
$taskZip = Join-Path $taskDist ($taskName + '.zip')
if (Test-Path -LiteralPath $taskZip) { throw "Release ZIP already exists: $taskZip. Choose a new version or explicitly move the old artifact after review." }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($taskStage, $taskZip, [IO.Compression.CompressionLevel]::Optimal, $false)
$taskDigest = (Get-FileHash -LiteralPath $taskZip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($taskZip + '.sha256'), "$taskDigest  $([IO.Path]::GetFileName($taskZip))`n", [Text.UTF8Encoding]::new($false))
Write-Host "Unsigned release package: $taskZip"
Write-Host "SHA-256: $taskDigest"
