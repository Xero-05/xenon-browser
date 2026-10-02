param([switch]$CoreOnly, [switch]$Test, [int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskVs = & $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVs) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
$taskCmake = Join-Path $taskVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$taskCtest = Join-Path (Split-Path $taskCmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $taskCmake)) { throw 'Install the Visual Studio CMake component.' }
$taskBuild = Join-Path $taskRoot 'build'
$browserFlag = if ($CoreOnly) { 'OFF' } else { 'ON' }
& $taskCmake -S $taskRoot -B $taskBuild -G 'Visual Studio 17 2022' -A x64 "-DXENON_BUILD_BROWSER=$browserFlag"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $taskCmake --build $taskBuild --config Release --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
Push-Location $taskRoot
try {
  & npm.cmd run build
  if ($LASTEXITCODE -ne 0) { throw 'MCP adapter build failed.' }
  if ($Test) {
    New-Item -ItemType Directory -Force -Path (Join-Path $taskRoot 'out') | Out-Null
    & $taskCtest --test-dir $taskBuild -C Release --output-on-failure --output-junit (Join-Path $taskRoot 'out/native-tests.xml')
    if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
    & node --test --test-reporter=spec --test-reporter-destination=stdout --test-reporter=junit --test-reporter-destination=out/adapter-tests.xml 'adapter/dist/test/*.test.js'
    if ($LASTEXITCODE -ne 0) { throw 'MCP adapter tests failed.' }
    & node --test --test-reporter=spec --test-reporter-destination=stdout --test-reporter=junit --test-reporter-destination=out/login-capture-tests.xml 'tests/login-capture-tests.mjs'
    if ($LASTEXITCODE -ne 0) { throw 'Login monitor tests failed.' }
    & node --test --test-reporter=spec --test-reporter-destination=stdout --test-reporter=junit --test-reporter-destination=out/human-autofill-tests.xml 'tests/human-autofill-tests.mjs'
    if ($LASTEXITCODE -ne 0) { throw 'Human autofill tests failed.' }
    if (-not $CoreOnly) {
      & node tests/branding-resources.mjs
      if ($LASTEXITCODE -ne 0) { throw 'Bootstrap branding integrity failed.' }
    }
  }
} finally { Pop-Location }
