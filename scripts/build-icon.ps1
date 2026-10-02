param(
  [string]$Source = (Join-Path $PSScriptRoot '../assets/branding/xenon-icon.png'),
  [string]$Destination = (Join-Path $PSScriptRoot '../assets/branding/xenon-icon.ico')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$taskSource = [Drawing.Bitmap]::new([IO.Path]::GetFullPath($Source))
$taskImages = [Collections.Generic.List[byte[]]]::new()
$taskSizes = @(16,20,24,32,40,48,64,128,256)
try {
  foreach ($taskSize in $taskSizes) {
    $taskBitmap = [Drawing.Bitmap]::new($taskSize,$taskSize,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $taskGraphics = [Drawing.Graphics]::FromImage($taskBitmap)
    $taskStream = [IO.MemoryStream]::new()
    try {
      $taskGraphics.Clear([Drawing.Color]::Transparent)
      $taskGraphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
      $taskGraphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
      $taskGraphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
      $taskGraphics.DrawImage($taskSource,[Drawing.Rectangle]::new(0,0,$taskSize,$taskSize))
      $taskBitmap.Save($taskStream,[Drawing.Imaging.ImageFormat]::Png)
      $taskImages.Add($taskStream.ToArray())
    } finally { $taskStream.Dispose(); $taskGraphics.Dispose(); $taskBitmap.Dispose() }
  }
  $taskOutput = [IO.File]::Create([IO.Path]::GetFullPath($Destination))
  $taskWriter = [IO.BinaryWriter]::new($taskOutput)
  try {
    $taskWriter.Write([uint16]0); $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]$taskSizes.Count)
    $taskOffset = 6 + 16 * $taskSizes.Count
    for ($taskIndex=0; $taskIndex -lt $taskSizes.Count; $taskIndex++) {
      $taskDimension = if ($taskSizes[$taskIndex] -eq 256) { 0 } else { $taskSizes[$taskIndex] }
      $taskWriter.Write([byte]$taskDimension); $taskWriter.Write([byte]$taskDimension)
      $taskWriter.Write([byte]0); $taskWriter.Write([byte]0)
      $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]32)
      $taskWriter.Write([uint32]$taskImages[$taskIndex].Length); $taskWriter.Write([uint32]$taskOffset)
      $taskOffset += $taskImages[$taskIndex].Length
    }
    foreach ($taskImage in $taskImages) { $taskWriter.Write($taskImage) }
  } finally { $taskWriter.Dispose(); $taskOutput.Dispose() }
} finally { $taskSource.Dispose() }
Write-Host "Created nine-size Windows icon: $Destination"
