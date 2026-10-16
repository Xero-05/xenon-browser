param(
  [Parameter(Mandatory=$true)][string]$WorkDirectory,
  [Parameter(Mandatory=$true)][string]$Python,
  [switch]$UseGitHubMirror,
  [ValidateRange(1,32)][int]$Jobs = 4,
  [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskLock = Get-Content -LiteralPath (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$taskRecipe = $taskLock.cef.mediaSourceBuild
$taskWork = [IO.Path]::GetFullPath($WorkDirectory)
if ($taskWork -notmatch '^[A-Za-z]:\\[A-Za-z0-9_.\\-]+$') {
  throw 'CEF source builds need a dedicated local directory with no spaces, for example E:\XenonCefMedia.'
}
if (-not $taskLock.cef.version.Contains('+g' + $taskRecipe.cefCommit.Substring(0,7) + '+') -or
    -not $taskLock.cef.version.Contains('.' + $taskRecipe.branch + '.')) {
  throw 'The media build recipe must match the pinned CEF version.'
}
$taskSdk = Join-Path ${env:ProgramFiles(x86)} ('Windows Kits/10/Include/' + $taskRecipe.windowsSdk)
if (-not (Test-Path -LiteralPath (Join-Path $taskSdk 'um/Windows.h'))) {
  throw "Install Windows SDK $($taskRecipe.windowsSdk) (C++ headers/libraries and Debugging Tools) before building CEF."
}
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) { throw 'Pass an installed Python 3 executable.' }
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskVs = & $taskVsWhere -latest -products '*' -version '[17,18)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVs) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
$taskVcVersion = (Get-Content -LiteralPath (Join-Path $taskVs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$taskAtlRoot = Join-Path $taskVs "VC/Tools/MSVC/$taskVcVersion/atlmfc"
foreach ($taskAtlFile in @('include/atldef.h','lib/x64/atls.lib')) {
  if (-not (Test-Path -LiteralPath (Join-Path $taskAtlRoot $taskAtlFile))) {
    throw 'Install the Visual Studio C++ ATL component (Microsoft.VisualStudio.Component.VC.ATL) before building CEF.'
  }
}
$taskGit = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
if ($CheckOnly) { Write-Output "Ready for CEF $($taskLock.cef.version) with H.264/AAC in $taskWork"; return }

# Never reuse an arbitrary checkout or delete a user's files. A matching marker
# permits resuming this exact recipe after a download or build interruption.
$taskMarker = Join-Path $taskWork 'xenon-media-build.json'
$taskRecipeText = $taskRecipe | ConvertTo-Json -Compress
if (Test-Path -LiteralPath $taskWork) {
  if (-not (Test-Path -LiteralPath $taskMarker) -or
      (Get-Content -LiteralPath $taskMarker -Raw).Trim() -cne $taskRecipeText) {
    throw 'Use a new empty path or a directory created with this exact media build recipe.'
  }
} else {
  New-Item -ItemType Directory -Path $taskWork | Out-Null
  [IO.File]::WriteAllText($taskMarker, $taskRecipeText)
}
. (Join-Path $PSScriptRoot 'dependency-download.ps1')
$taskAutomate = Get-XenonDependency -Url "https://raw.githubusercontent.com/chromiumembedded/cef/$($taskRecipe.cefCommit)/tools/automate/automate-git.py" `
  -Name 'automate-git.py' -ExpectedHash $taskRecipe.automateSha256 -DownloadDirectory (Join-Path $taskWork 'tools')
$taskDepot = Join-Path $taskWork 'depot_tools'
if (-not (Test-Path -LiteralPath $taskDepot)) {
  & $taskGit clone --no-checkout https://chromium.googlesource.com/chromium/tools/depot_tools.git $taskDepot
  if ($LASTEXITCODE -ne 0) { throw 'depot_tools clone failed.' }
  & $taskGit -C $taskDepot checkout --detach $taskRecipe.depotToolsCommit
  if ($LASTEXITCODE -ne 0) { throw 'Pinned depot_tools checkout failed.' }
}
$taskDepotRevision = & $taskGit -C $taskDepot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $taskDepotRevision -ne $taskRecipe.depotToolsCommit) { throw 'depot_tools does not match the pinned recipe.' }

$taskEnvironment = @{
  DEPOT_TOOLS_WIN_TOOLCHAIN = '0'; DEPOT_TOOLS_UPDATE = '0';
  GN_DEFINES = $taskRecipe.gnDefines; GN_OUT_CONFIGS = 'Release_GN_x64'; PYTHONUNBUFFERED = '1';
  GYP_MSVS_VERSION = '2022'; vs2022_install = $taskVs;
  PATH = $taskDepot + ';' + (Join-Path $taskDepot 'python-bin') + ';' + $env:PATH
}
$taskSaved = @{}
try {
  foreach ($taskName in $taskEnvironment.Keys) {
    $taskSaved[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process')
    [Environment]::SetEnvironmentVariable($taskName, $taskEnvironment[$taskName], 'Process')
  }
  # Disabling automatic Git updates also skips depot_tools' initial Windows
  # Python setup. Run its pinned bootstrap explicitly; do not add local shims.
  & (Join-Path $taskDepot 'bootstrap/win_tools.bat')
  if ($LASTEXITCODE -ne 0) { throw 'Pinned depot_tools Python bootstrap failed.' }
  # Install gsutil once before parallel dependency jobs contend for its short
  # install lock. This uses upstream's own download and integrity checks.
  & (Join-Path $taskDepot 'python3.bat') (Join-Path $taskDepot 'gsutil.py') version
  if ($LASTEXITCODE -ne 0) { throw 'Pinned gsutil initialization failed.' }
  # These prebuilt Chrome updater executables belong only to the branded,
  # testonly old_updater target. CEF does not use them. Keep them out of the
  # checkout before the first sync, without changing upstream source or AV
  # settings. This depot_tools version does not honor custom_deps=None for CIPD.
  $taskChromiumUrl = 'https://chromium.googlesource.com/chromium/src.git'
  if ($UseGitHubMirror) { $taskChromiumUrl = 'https://github.com/chromium/chromium.git' }
  $taskChromiumVersion = ($taskLock.cef.version -split '\+chromium-')[1]
  $taskClientDirectory = Join-Path $taskWork 'chromium'
  New-Item -ItemType Directory -Path $taskClientDirectory -Force | Out-Null
  $taskSource = Join-Path $taskClientDirectory 'src'
  if (-not (Test-Path -LiteralPath $taskSource)) {
    & $taskGit clone --depth 1 --branch $taskChromiumVersion $taskChromiumUrl $taskSource
    if ($LASTEXITCODE -ne 0) { throw 'Chromium source checkout failed.' }
  }
  $taskChromiumRevision = & $taskGit -C $taskSource rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $taskChromiumRevision -ne $taskRecipe.chromiumCommit) { throw 'Chromium does not match the pinned source revision.' }
  & $Python (Join-Path $PSScriptRoot 'cef-media-deps.py') (Join-Path $taskSource 'DEPS') --pristine
  if ($LASTEXITCODE -ne 0) { throw 'Media dependency manifest generation failed.' }
  $taskClientConfig = "solutions = [{'managed': False, 'name': 'src', 'url': '${taskChromiumUrl}@${taskChromiumVersion}', 'custom_vars': {'checkout_pgo_profiles': False, 'source_tarball': False, 'siso_version': 'latest'}, 'custom_deps': {}, 'deps_file': 'DEPS.xenon-media', 'safesync_url': ''}]"
  [IO.File]::WriteAllText((Join-Path $taskClientDirectory '.gclient'), $taskClientConfig)
  $taskArguments = @($taskAutomate, "--download-dir=$taskWork", "--depot-tools-dir=$taskDepot",
    "--branch=$($taskRecipe.branch)", "--checkout=$($taskRecipe.cefCommit)",
    # automate-git appends its target list to autoninja. Pass its explicit job
    # limit there; NINJA_CORE_LIMIT only limits remote builds, not local ones.
    '--x64-build', '--no-chromium-history', '--no-debug-build', "--build-target=libcef -j $Jobs",
    '--minimal-distrib-only', '--build-log-file')
  if ($UseGitHubMirror) { $taskArguments += '--chromium-url=https://github.com/chromium/chromium.git' }
  # Upstream's shallow checkout requires the version tag in the pinned CEF
  # compatibility file. Verify its resolved commit before compiling anything.
  & $Python @taskArguments --no-chromium-update --no-build --no-distrib
  if ($LASTEXITCODE -ne 0) { throw 'CEF source download failed. Reuse this directory to resume.' }
  $taskChromiumRevision = & $taskGit -C (Join-Path $taskWork 'chromium/src') rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $taskChromiumRevision -ne $taskRecipe.chromiumCommit) { throw 'Chromium does not match the pinned source revision.' }
  # automate-git considers an existing shallow checkout up to date even if an
  # earlier dependency sync failed. Finish dependencies/hooks explicitly before
  # entering the build phase, including on resumed source downloads.
  $taskSource = Join-Path $taskWork 'chromium/src'
  & (Join-Path $taskDepot 'python3.bat') (Join-Path $taskWork 'cef/tools/patcher.py') `
    --patch-file (Join-Path $taskWork 'cef/patch/patches/runhooks') --patch-dir $taskSource
  if ($LASTEXITCODE -ne 0) { throw 'CEF runhooks preparation failed.' }
  Push-Location (Join-Path $taskWork 'chromium')
  try {
    & (Join-Path $taskDepot 'gclient.bat') sync --nohooks --no-history
    if ($LASTEXITCODE -ne 0) { throw 'Chromium dependency sync failed.' }
    & (Join-Path $taskDepot 'gclient.bat') runhooks
    if ($LASTEXITCODE -ne 0) { throw 'Chromium dependency hooks failed.' }
  } finally { Pop-Location }
  $taskBuildLog = Join-Path $taskWork "build-$($taskRecipe.branch)-release.log"
  if (Test-Path -LiteralPath $taskBuildLog) {
    Copy-Item -LiteralPath $taskBuildLog -Destination ($taskBuildLog + '.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff') + '.previous')
  }
  & $Python @taskArguments --no-update --force-build --force-distrib
  if ($LASTEXITCODE -ne 0) { throw 'CEF media build failed. The dedicated directory can be reused to resume.' }
  $taskDistribution = Join-Path $taskWork ('chromium/src/cef/binary_distrib/cef_binary_' + $taskLock.cef.version + '_windows64_minimal')
  foreach ($taskFile in @('Release/libcef.dll','Release/libcef.lib','Release/bootstrap.exe','include/cef_version.h','LICENSE.txt','CREDITS.html')) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskDistribution $taskFile))) { throw "Incomplete CEF distribution: $taskFile" }
  }
  Write-Output "Complete media-enabled CEF distribution: $taskDistribution"
  Write-Output 'Rebuild Xenon against this complete distribution, then run the media and browser acceptance suites before packaging.'
} finally {
  foreach ($taskName in $taskSaved.Keys) { [Environment]::SetEnvironmentVariable($taskName, $taskSaved[$taskName], 'Process') }
}
