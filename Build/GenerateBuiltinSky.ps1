# 生成连续方向场的六面柔和日光天空，不包含太阳圆盘
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$output = Join-Path $PSScriptRoot '../OrbedenEditor/Templates/Builtin/Skyboxes/SoftDaylight'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$size = 128
$faces = @('right', 'left', 'top', 'bottom', 'front', 'back')
foreach ($face in $faces) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    try {
        for ($row = 0; $row -lt $size; $row++) {
            for ($column = 0; $column -lt $size; $column++) {
                $u = 2 * ($column + 0.5) / $size - 1
                $v = 2 * ($row + 0.5) / $size - 1
                $direction = switch ($face) {
                    'right' { @(1, -$v, -$u) }
                    'left' { @(-1, -$v, $u) }
                    'top' { @($u, 1, $v) }
                    'bottom' { @($u, -1, -$v) }
                    'front' { @($u, -$v, 1) }
                    'back' { @(-$u, -$v, -1) }
                }
                $length = [Math]::Sqrt($direction[0] * $direction[0] + $direction[1] * $direction[1] + $direction[2] * $direction[2])
                $x = $direction[0] / $length
                $y = $direction[1] / $length
                $z = $direction[2] / $length
                $horizon = @(0.73, 0.79, 0.83)
                $pole = if ($y -ge 0) { @(0.22, 0.43, 0.70) } else { @(0.24, 0.23, 0.21) }
                $blend = [Math]::Pow([Math]::Abs($y), 0.45)
                $cloud = [Math]::Max(0.0, [Math]::Sin(9 * $x + 3 * $z) + 0.5 * [Math]::Sin(17 * $z - 6 * $x) - 0.25)
                $cloud = [Math]::Min(0.7, $cloud * 0.6) * [Math]::Max(0.0, [Math]::Min(1.0, $y * 5))
                $rgb = for ($channel = 0; $channel -lt 3; $channel++) {
                    $value = ($horizon[$channel] * (1 - $blend) + $pole[$channel] * $blend) * (1 - $cloud) + 0.92 * $cloud
                    [int][Math]::Round([Math]::Clamp([double]$value, 0.0, 1.0) * 255)
                }
                $bitmap.SetPixel($column, $row, [System.Drawing.Color]::FromArgb($rgb[0], $rgb[1], $rgb[2]))
            }
        }
        $bitmap.Save((Join-Path $output "$face.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $bitmap.Dispose() }
}
