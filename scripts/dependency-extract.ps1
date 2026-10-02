function Expand-XenonCefArchive {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$Destination,
    [ValidateRange(1,900)][int]$TimeoutSeconds = 300
  )
  $ErrorActionPreference = 'Stop'
  # Git also ships GNU tar. Its drive-letter/remote-archive interpretation is
  # unsuitable for these absolute Windows paths. Choose Windows bsdtar by path.
  $taskTar = Join-Path ([Environment]::SystemDirectory) 'tar.exe'
  if (-not (Test-Path -LiteralPath $taskTar -PathType Leaf)) { throw 'Windows System32/tar.exe is required.' }
  $taskArchive = (Get-Item -LiteralPath $Archive -ErrorAction Stop).FullName
  $taskDestination = [IO.Path]::GetFullPath($Destination)
  New-Item -ItemType Directory -Force -Path $taskDestination | Out-Null
  Write-Host "[extract] Using $taskTar"
  & $taskTar --version
  if ($LASTEXITCODE -ne 0) { throw 'Could not identify the Windows tar version.' }
  $taskArguments = foreach ($taskArgument in @('-xf',$taskArchive,'--strip-components=1')) {
    $taskQuoted = [Regex]::Replace($taskArgument, '(\\*)"', '$1$1\"')
    '"' + [Regex]::Replace($taskQuoted, '(\\+)$', '$1$1') + '"'
  }
  $taskStart = [Diagnostics.ProcessStartInfo]::new()
  $taskStart.FileName = $taskTar
  $taskStart.Arguments = $taskArguments -join ' '
  $taskStart.WorkingDirectory = $taskDestination
  $taskStart.UseShellExecute = $false
  $taskStart.CreateNoWindow = $true
  $taskStart.RedirectStandardOutput = $true
  $taskStart.RedirectStandardError = $true
  $taskProcess = [Diagnostics.Process]::new()
  $taskProcess.StartInfo = $taskStart
  $taskClock = [Diagnostics.Stopwatch]::StartNew()
  $taskStarted = $false
  $taskHeartbeat = 30
  try {
    if (-not $taskProcess.Start()) { throw 'Could not start Windows tar.' }
    $taskStarted = $true
    $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
    $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
    while (-not $taskProcess.WaitForExit(1000)) {
      if ($taskClock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
        throw "CEF extraction exceeded ${TimeoutSeconds}s using $taskTar. Incomplete extraction is not marked ready."
      }
      if ($taskClock.Elapsed.TotalSeconds -ge $taskHeartbeat) {
        $taskProgress = Get-ChildItem -LiteralPath $taskDestination -File -Recurse | Measure-Object -Property Length -Sum
        Write-Host "[extract] CEF elapsed $([int]$taskClock.Elapsed.TotalSeconds)s; $($taskProgress.Count) files, $($taskProgress.Sum) bytes written"
        $taskHeartbeat += 30
      }
    }
    $taskProcess.WaitForExit()
    $taskOutput = $taskStdout.GetAwaiter().GetResult().Trim()
    $taskDiagnostic = $taskStderr.GetAwaiter().GetResult().Trim()
    if ($taskOutput) { Write-Host $taskOutput }
    if ($taskProcess.ExitCode -ne 0) { throw "CEF extraction failed (exit $($taskProcess.ExitCode)): $taskDiagnostic" }
    Write-Host "[extract] Windows tar completed in $([Math]::Round($taskClock.Elapsed.TotalSeconds,1))s"
  } finally {
    if ($taskStarted -and -not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit() }
    $taskProcess.Dispose()
  }
}
