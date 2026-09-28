param(
    [string]$Source = (Join-Path $PSScriptRoot '../Docs/Brand/orbeden-icon-white.svg'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '../OrbedenEditor/Resources')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore, WindowsBase, System.Drawing

# 读取 SVG 路径并转换为可缩放几何
[xml]$svg = Get-Content -LiteralPath $Source -Raw
if ($svg.DocumentElement.GetAttribute('viewBox') -ne '0 0 256 256') { throw 'Expected a 256 x 256 SVG viewBox.' }
$geometries = @($svg.SelectNodes('//*[local-name()="path"]') | ForEach-Object {
    [Windows.Media.Geometry]::Parse($_.GetAttribute('d'))
})
if ($geometries.Count -eq 0) { throw 'The SVG has no paths.' }
$resourceDirectory = [IO.Path]::GetFullPath($OutputRoot)
[IO.Directory]::CreateDirectory((Join-Path $resourceDirectory 'Icons/256')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $resourceDirectory 'Brand')) | Out-Null

# 渲染四倍尺寸后缩小，输出白色透明 PNG 或黑底白标的系统图标帧
function ConvertTo-BrandPng([int]$Size, [bool]$Badge) {
    $visual = [Windows.Media.DrawingVisual]::new()
    $context = $visual.RenderOpen()
    try {
        $context.PushTransform([Windows.Media.ScaleTransform]::new($Size * 4.0 / 256, $Size * 4.0 / 256))
        if ($Badge) {
            $context.DrawRoundedRectangle([Windows.Media.Brushes]::Black, $null, [Windows.Rect]::new(0, 0, 256, 256), 44, 44)
        }
        foreach ($geometry in $geometries) { $context.DrawGeometry([Windows.Media.Brushes]::White, $null, $geometry) }
        $context.Pop()
    }
    finally { $context.Close() }
    $render = [Windows.Media.Imaging.RenderTargetBitmap]::new($Size * 4, $Size * 4, 96, 96, [Windows.Media.PixelFormats]::Pbgra32)
    $render.Render($visual)
    $encoder = [Windows.Media.Imaging.PngBitmapEncoder]::new()
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($render))
    $sourceStream = [IO.MemoryStream]::new()
    $encoder.Save($sourceStream)
    $sourceStream.Position = 0
    $sourceBitmap = [Drawing.Bitmap]::new($sourceStream)
    $bitmap = [Drawing.Bitmap]::new($Size, $Size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $outputStream = [IO.MemoryStream]::new()
    try {
        $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.DrawImage($sourceBitmap, [Drawing.Rectangle]::new(0, 0, $Size, $Size), 0, 0, $sourceBitmap.Width, $sourceBitmap.Height, [Drawing.GraphicsUnit]::Pixel)
        $bitmap.Save($outputStream, [Drawing.Imaging.ImageFormat]::Png)
        return ,$outputStream.ToArray()
    }
    finally {
        $outputStream.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
        $sourceBitmap.Dispose()
        $sourceStream.Dispose()
    }
}

# 导出欢迎页纹理
[IO.File]::WriteAllBytes((Join-Path $resourceDirectory 'Icons/Orbeden.png'), (ConvertTo-BrandPng 32 $false))
[IO.File]::WriteAllBytes((Join-Path $resourceDirectory 'Icons/256/Orbeden.png'), (ConvertTo-BrandPng 256 $false))

# 写入 Windows 多尺寸 ICO 目录与 PNG 帧
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$frames = @($sizes | ForEach-Object { ,(ConvertTo-BrandPng $_ $true) })
$iconPath = Join-Path $resourceDirectory 'Brand/Orbeden.ico'
$writer = [IO.BinaryWriter]::new([IO.File]::Create($iconPath))
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; ++$index) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $frames[$index].Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
}
finally { $writer.Dispose() }
Write-Host "Exported Orbeden PNG textures and multi-size ICO to $resourceDirectory"
