function Expand-XenonCefArchive {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$Destination,
    [ValidateRange(1,900)][int]$TimeoutSeconds = 300
  )
  $ErrorActionPreference = 'Stop'
  # The hosted Windows tar can stall while decoding large bzip2 archives.
  # Decompress to disk with 7-Zip, then give Windows tar an uncompressed archive.
  # Choose tools by path so Git's GNU tar cannot reinterpret drive-letter paths.
  $taskTar = Join-Path ([Environment]::SystemDirectory) 'tar.exe'
  $taskSevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
  if (-not (Test-Path -LiteralPath $taskTar -PathType Leaf)) { throw 'Windows System32/tar.exe is required.' }
  if (-not (Test-Path -LiteralPath $taskSevenZip -PathType Leaf)) { throw 'Install 7-Zip in Program Files/7-Zip before running bootstrap.' }
  $taskArchive = (Get-Item -LiteralPath $Archive -ErrorAction Stop).FullName
  $taskDestination = [IO.Path]::GetFullPath($Destination)
  New-Item -ItemType Directory -Force -Path $taskDestination | Out-Null
  $taskStage = Join-Path $taskDestination ('.xenon-unpack-' + [Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $taskStage | Out-Null
  # bzip2 has no embedded filename; 7-Zip removes the .bz2 suffix. Track this
  # exact path before starting so failed decompression cannot leave a huge tar.
  $taskTarArchive = Join-Path $taskStage ([IO.Path]::GetFileNameWithoutExtension($taskArchive))
  $taskDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

  function Invoke-XenonExtractionProcess {
    param([string]$Executable,[string[]]$Arguments,[string]$Stage,[string]$ProgressDirectory)
    if ([DateTime]::UtcNow -ge $taskDeadline) { throw "CEF extraction exceeded $($TimeoutSeconds)s. Incomplete extraction is not marked ready." }
    Write-Host "[extract] $Stage using $Executable"
    $taskArguments = foreach ($taskArgument in $Arguments) {
      $taskQuoted = [Regex]::Replace($taskArgument, '(\\*)"', '$1$1\"')
      '"' + [Regex]::Replace($taskQuoted, '(\\+)$', '$1$1') + '"'
    }
    $taskStart = [Diagnostics.ProcessStartInfo]::new()
    $taskStart.FileName = $Executable
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
      if (-not $taskProcess.Start()) { throw "Could not start $Executable." }
      $taskStarted = $true
      $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
      $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
      while (-not $taskProcess.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -ge $taskDeadline) {
          throw "CEF extraction exceeded $($TimeoutSeconds)s during $Stage. Incomplete extraction is not marked ready."
        }
        if ($taskClock.Elapsed.TotalSeconds -ge $taskHeartbeat) {
          $taskProgress = Get-ChildItem -LiteralPath $ProgressDirectory -File -Recurse -Force | Measure-Object -Property Length -Sum
          Write-Host "[extract] $Stage elapsed $([int]$taskClock.Elapsed.TotalSeconds)s; $($taskProgress.Count) files, $($taskProgress.Sum) bytes written"
          $taskHeartbeat += 30
        }
      }
      $taskProcess.WaitForExit()
      $taskOutput = $taskStdout.GetAwaiter().GetResult().Trim()
      $taskDiagnostic = $taskStderr.GetAwaiter().GetResult().Trim()
      if ($taskOutput) { Write-Host $taskOutput }
      if ($taskProcess.ExitCode -ne 0) { throw "$Stage failed (exit $($taskProcess.ExitCode)): $taskDiagnostic" }
      Write-Host "[extract] $Stage completed in $([Math]::Round($taskClock.Elapsed.TotalSeconds,1))s"
    } finally {
      if ($taskStarted -and -not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit() }
      $taskProcess.Dispose()
    }
  }

  try {
    Invoke-XenonExtractionProcess -Executable $taskSevenZip -Arguments @('x','-y','-bsp0','-bd','-tbzip2',$taskArchive,('-o' + $taskStage)) -Stage 'CEF bzip2 decompression' -ProgressDirectory $taskStage
    $taskTarFiles = @(Get-ChildItem -LiteralPath $taskStage -File)
    if ($taskTarFiles.Count -ne 1 -or $taskTarFiles[0].Extension -ne '.tar' -or $taskTarFiles[0].FullName -ne $taskTarArchive) { throw 'CEF decompression did not produce the expected tar archive.' }
    Invoke-XenonExtractionProcess -Executable $taskTar -Arguments @('--version') -Stage 'Windows tar version' -ProgressDirectory $taskDestination
    Invoke-XenonExtractionProcess -Executable $taskTar -Arguments @('-xf',$taskTarArchive,'--strip-components=1') -Stage 'CEF tar extraction' -ProgressDirectory $taskDestination
  } finally {
    if ($taskTarArchive -and (Test-Path -LiteralPath $taskTarArchive -PathType Leaf)) { Remove-Item -LiteralPath $taskTarArchive -Force }
    if ((Test-Path -LiteralPath $taskStage -PathType Container) -and @(Get-ChildItem -LiteralPath $taskStage -Force).Count -eq 0) { Remove-Item -LiteralPath $taskStage -Force }
  }
}
