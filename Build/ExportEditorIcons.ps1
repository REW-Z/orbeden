param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [string]$OutputRoot = (Join-Path $PSScriptRoot '../OrbedenEditor/Resources/Icons')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sourceDirectory = (Resolve-Path -LiteralPath $SourceRoot).Path
$outputDirectory = [IO.Path]::GetFullPath($OutputRoot)

# 从高清原图导出两个尺寸，保留透明通道和宽高比
foreach ($file in Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File -Filter '*.png') {
    $relativePath = [IO.Path]::GetRelativePath($sourceDirectory, $file.FullName)
    $sourceImage = [Drawing.Bitmap]::new($file.FullName)
    try {
        foreach ($size in @(32, 256)) {
            $destination = if ($size -eq 32) { Join-Path $outputDirectory $relativePath } else { Join-Path (Join-Path $outputDirectory '256') $relativePath }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
            $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $attributes = [Drawing.Imaging.ImageAttributes]::new()
            try {
                $graphics.Clear([Drawing.Color]::Transparent)
                $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $attributes.SetWrapMode([Drawing.Drawing2D.WrapMode]::TileFlipXY)
                $scale = [Math]::Min($size / $sourceImage.Width, $size / $sourceImage.Height)
                $width = [int][Math]::Round($sourceImage.Width * $scale)
                $height = [int][Math]::Round($sourceImage.Height * $scale)
                $rectangle = [Drawing.Rectangle]::new([int](($size - $width) / 2), [int](($size - $height) / 2), $width, $height)
                $graphics.DrawImage($sourceImage, $rectangle, 0, 0, $sourceImage.Width, $sourceImage.Height, [Drawing.GraphicsUnit]::Pixel, $attributes)
                $bitmap.Save($destination, [Drawing.Imaging.ImageFormat]::Png)
            }
            finally { $attributes.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
        }
    }
    finally { $sourceImage.Dispose() }
}
