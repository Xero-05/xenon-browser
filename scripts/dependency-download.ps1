# Shared by bootstrap and the loopback-only downloader regression harness.
# curl.exe is included with supported Windows versions. Never read .curlrc.
function Get-XenonDependency {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory=$true)][string]$Url,
    [Parameter(Mandatory=$true)][string]$Name,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedHash,
    [Parameter(Mandatory=$true)][string]$DownloadDirectory,
    [ValidateRange(1,300)][int]$ConnectTimeoutSeconds = 30,
    [ValidateRange(1,300)][int]$StallTimeoutSeconds = 45,
    [ValidateRange(1,900)][int]$AttemptTimeoutSeconds = 300,
    [ValidateRange(1,5)][int]$MaxAttempts = 3,
    [ValidateRange(0,30)][int]$RetryDelaySeconds = 2,
    [switch]$AllowLoopbackHttp
  )
  $ErrorActionPreference = 'Stop'
  $taskUri = [Uri]$Url
  $taskTestHttp = $AllowLoopbackHttp -and $taskUri.Scheme -eq 'http' -and
    $taskUri.Host -in @('127.0.0.1','[::1]','::1','localhost')
  if (-not $taskUri.IsAbsoluteUri -or $taskUri.UserInfo -or
      ($taskUri.Scheme -ne 'https' -and -not $taskTestHttp)) {
    throw 'Dependencies require an HTTPS URL without embedded credentials.'
  }
  if ($Name -ne [IO.Path]::GetFileName($Name) -or $Name -in @('.','..') -or
      $Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or -not $Name) {
    throw 'Dependency name must be one ordinary file name.'
  }
  $taskDirectory = [IO.Path]::GetFullPath($DownloadDirectory)
  New-Item -ItemType Directory -Force -Path $taskDirectory | Out-Null
  $taskTarget = Join-Path $taskDirectory $Name
  if (Test-Path -LiteralPath $taskTarget) {
    $taskExisting = Get-Item -LiteralPath $taskTarget -Force
    if ($taskExisting.PSIsContainer -or ($taskExisting.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "Dependency cache entry is not a regular file: $Name"
    }
    Write-Host "[cache] Verifying SHA-256 for $Name ($($taskExisting.Length) bytes)"
    if ((Get-FileHash -LiteralPath $taskTarget -Algorithm SHA256).Hash -ieq $ExpectedHash) {
      Write-Host "[cache] Verified $Name"
      return $taskTarget
    }
    Write-Warning "Discarding corrupt cache entry: $Name"
    Remove-Item -LiteralPath $taskTarget
  }
  $taskCurl = (Get-Command curl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
  $taskPart = Join-Path $taskDirectory ($Name + '.' + [Guid]::NewGuid().ToString('N') + '.part')
  $taskProtocol = if ($taskTestHttp) { '=http,https' } else { '=https' }
  $taskTotal = [Diagnostics.Stopwatch]::StartNew()
  try {
    for ($taskAttempt = 1; $taskAttempt -le $MaxAttempts; $taskAttempt++) {
      Write-Host "[download] $Name attempt $taskAttempt/$MaxAttempts from $($taskUri.Host); connect ${ConnectTimeoutSeconds}s, low-speed ${StallTimeoutSeconds}s, total ${AttemptTimeoutSeconds}s"
      $taskCurlArguments = @('--disable','--fail','--location','--silent','--show-error',
        '--proto',$taskProtocol,'--proto-redir',$taskProtocol,
        '--connect-timeout',"$ConnectTimeoutSeconds",'--max-time',"$AttemptTimeoutSeconds",
        '--speed-limit','1024','--speed-time',"$StallTimeoutSeconds",
        '--output',$taskPart,'--write-out','%{http_code}',$taskUri.AbsoluteUri)
      # ProcessStartInfo.ArgumentList is unavailable in Windows PowerShell 5.1.
      # Quote directly for the Windows argv parser; do not invoke a shell.
      $taskQuotedArguments = foreach ($taskArgument in $taskCurlArguments) {
        $taskQuoted = [Regex]::Replace($taskArgument, '(\\*)"', '$1$1\"')
        '"' + [Regex]::Replace($taskQuoted, '(\\+)$', '$1$1') + '"'
      }
      $taskStart = [Diagnostics.ProcessStartInfo]::new()
      $taskStart.FileName = $taskCurl
      $taskStart.Arguments = $taskQuotedArguments -join ' '
      $taskStart.UseShellExecute = $false
      $taskStart.CreateNoWindow = $true
      $taskStart.RedirectStandardOutput = $true
      $taskStart.RedirectStandardError = $true
      $taskProcess = [Diagnostics.Process]::new()
      $taskProcess.StartInfo = $taskStart
      $taskAttemptClock = [Diagnostics.Stopwatch]::StartNew()
      $taskHeartbeat = 10
      $taskWatchdog = $false
      $taskStarted = $false
      try {
        if (-not $taskProcess.Start()) { throw 'Could not start the dependency downloader.' }
        $taskStarted = $true
        $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
        $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
        while (-not $taskProcess.WaitForExit(1000)) {
          if ($taskAttemptClock.Elapsed.TotalSeconds -ge $AttemptTimeoutSeconds + 5) {
            $taskProcess.Kill()
            $taskWatchdog = $true
            break
          }
          if ($taskAttemptClock.Elapsed.TotalSeconds -ge $taskHeartbeat) {
            $taskBytes = if (Test-Path -LiteralPath $taskPart) { (Get-Item -LiteralPath $taskPart).Length } else { 0 }
            Write-Host "[download] $Name attempt $taskAttempt elapsed $([int]$taskAttemptClock.Elapsed.TotalSeconds)s; $taskBytes bytes received"
            $taskHeartbeat += 10
          }
        }
        $taskProcess.WaitForExit()
        $taskExit = if ($taskWatchdog) { 28 } else { $taskProcess.ExitCode }
        $taskHttp = $taskStdout.GetAwaiter().GetResult().Trim()
        $taskDiagnostic = $taskStderr.GetAwaiter().GetResult().Trim()
      } finally {
        if ($taskStarted -and -not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit() }
        $taskProcess.Dispose()
      }
      $taskBytes = if (Test-Path -LiteralPath $taskPart) { (Get-Item -LiteralPath $taskPart).Length } else { 0 }
      Write-Host "[download] $Name attempt $taskAttempt finished in $([Math]::Round($taskAttemptClock.Elapsed.TotalSeconds,1))s; curl=$taskExit HTTP=$taskHttp bytes=$taskBytes"
      if ($taskExit -eq 0) {
        Write-Host "[verify] Checking SHA-256 for $Name"
        if ((Get-FileHash -LiteralPath $taskPart -Algorithm SHA256).Hash -ine $ExpectedHash) {
          throw "SHA-256 mismatch for $Name; untrusted download discarded."
        }
        # Same-directory rename publishes only a complete, verified file.
        Move-Item -LiteralPath $taskPart -Destination $taskTarget
        Write-Host "[ready] $Name verified in $([Math]::Round($taskTotal.Elapsed.TotalSeconds,1))s"
        return $taskTarget
      }
      if (Test-Path -LiteralPath $taskPart) { Remove-Item -LiteralPath $taskPart }
      $taskTransient = $taskExit -in @(5,6,7,18,28,35,52,55,56) -or
        ($taskExit -eq 22 -and $taskHttp -in @('408','429','500','502','503','504'))
      if (-not $taskTransient -or $taskAttempt -eq $MaxAttempts) {
        if ($taskDiagnostic.Length -gt 600) { $taskDiagnostic = $taskDiagnostic.Substring(0,600) }
        throw "Download failed for $Name after $taskAttempt attempt(s): curl=$taskExit HTTP=$taskHttp. $taskDiagnostic"
      }
      Write-Warning "Transient transfer failure for $Name; retrying with a new partial file."
      Start-Sleep -Seconds ($RetryDelaySeconds * $taskAttempt)
    }
  } finally {
    if (Test-Path -LiteralPath $taskPart) { Remove-Item -LiteralPath $taskPart }
  }
}
