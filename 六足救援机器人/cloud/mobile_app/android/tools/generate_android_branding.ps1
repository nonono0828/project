param(
    [string]$ResourceRoot = (Join-Path $PSScriptRoot '..\android\app\src\main\res'),
    [string]$PreviewPath = (Join-Path $PSScriptRoot '..\design\spider-001-app-icon-512.png')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$navy = [System.Drawing.Color]::FromArgb(255, 7, 24, 42)
$navyBright = [System.Drawing.Color]::FromArgb(255, 12, 51, 82)
$cyan = [System.Drawing.Color]::FromArgb(255, 105, 199, 255)
$cyanBright = [System.Drawing.Color]::FromArgb(255, 109, 236, 218)
$white = [System.Drawing.Color]::FromArgb(255, 242, 249, 255)
$muted = [System.Drawing.Color]::FromArgb(255, 105, 141, 171)
$appTitle = (
    [char]0x667A,
    [char]0x5DE1,
    [char]0x7CBE,
    [char]0x7075
) -join ''

function New-Canvas([int]$Width, [int]$Height, [bool]$Transparent = $false) {
    $bitmap = [System.Drawing.Bitmap]::new(
        $Width,
        $Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    if ($Transparent) { $graphics.Clear([System.Drawing.Color]::Transparent) }
    return @($bitmap, $graphics)
}

function Fill-Background(
    [System.Drawing.Graphics]$Graphics,
    [int]$Width,
    [int]$Height,
    [bool]$Round = $false
) {
    $rect = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
    $brush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $rect,
        $navyBright,
        $navy,
        145
    )
    if ($Round) {
        $Graphics.FillEllipse($brush, 0, 0, $Width, $Height)
    } else {
        $Graphics.FillRectangle($brush, $rect)
    }
    $brush.Dispose()

    $glow = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(24, $cyan))
    $Graphics.FillEllipse(
        $glow,
        [int]($Width * 0.52),
        [int](-$Height * 0.16),
        [int]($Width * 0.70),
        [int]($Height * 0.70)
    )
    $glow.Dispose()
}

function Draw-Spider(
    [System.Drawing.Graphics]$Graphics,
    [float]$CenterX,
    [float]$CenterY,
    [float]$Size
) {
    $legPen = [System.Drawing.Pen]::new($cyan, [Math]::Max(2.0, $Size * 0.026))
    $legPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $legPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $jointPen = [System.Drawing.Pen]::new(
        [System.Drawing.Color]::FromArgb(155, $cyanBright),
        [Math]::Max(1.4, $Size * 0.020)
    )
    $jointPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $jointPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

    $legStarts = @(-0.25, -0.08, 0.10, 0.27)
    $legKnees = @(-0.53, -0.62, -0.62, -0.50)
    $legFeet = @(-0.45, -0.13, 0.18, 0.47)
    foreach ($side in @(-1, 1)) {
        $x0 = $CenterX + $side * $Size * 0.15
        for ($index = 0; $index -lt 4; $index++) {
            $start = [double]$legStarts[$index]
            $knee = [double]$legKnees[$index]
            $foot = [double]$legFeet[$index]
            $y0 = $CenterY + $Size * $start
            $x1 = $CenterX + $side * $Size * 0.39
            $y1 = $CenterY + $Size * $knee
            $x2 = $CenterX + $side * $Size * 0.64
            $y2 = $CenterY + $Size * $foot
            $Graphics.DrawLine($legPen, $x0, $y0, $x1, $y1)
            $Graphics.DrawLine($jointPen, $x1, $y1, $x2, $y2)
        }
    }

    $bodyBrush = [System.Drawing.SolidBrush]::new($cyan)
    $headBrush = [System.Drawing.SolidBrush]::new($cyanBright)
    $darkBrush = [System.Drawing.SolidBrush]::new($navy)
    $shineBrush = [System.Drawing.SolidBrush]::new($white)

    $Graphics.FillEllipse(
        $bodyBrush,
        $CenterX - $Size * 0.18,
        $CenterY - $Size * 0.23,
        $Size * 0.36,
        $Size * 0.55
    )
    $Graphics.FillEllipse(
        $headBrush,
        $CenterX - $Size * 0.135,
        $CenterY - $Size * 0.37,
        $Size * 0.27,
        $Size * 0.25
    )
    $Graphics.FillEllipse(
        $darkBrush,
        $CenterX - $Size * 0.075,
        $CenterY - $Size * 0.285,
        $Size * 0.15,
        $Size * 0.15
    )
    $Graphics.FillEllipse(
        $shineBrush,
        $CenterX - $Size * 0.025,
        $CenterY - $Size * 0.26,
        $Size * 0.04,
        $Size * 0.04
    )

    $legPen.Dispose()
    $jointPen.Dispose()
    $bodyBrush.Dispose()
    $headBrush.Dispose()
    $darkBrush.Dispose()
    $shineBrush.Dispose()
}

function Save-Png(
    [System.Drawing.Bitmap]$Bitmap,
    [System.Drawing.Graphics]$Graphics,
    [string]$Path
) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $Bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $Graphics.Dispose()
    $Bitmap.Dispose()
}

$densities = @{
    'mdpi' = 48
    'hdpi' = 72
    'xhdpi' = 96
    'xxhdpi' = 144
    'xxxhdpi' = 192
}

foreach ($item in $densities.GetEnumerator()) {
    $density = $item.Key
    $size = [int]$item.Value
    $folder = Join-Path $ResourceRoot "mipmap-$density"

    $canvas = New-Canvas $size $size
    Fill-Background $canvas[1] $size $size
    Draw-Spider $canvas[1] ($size / 2) ($size * 0.51) ($size * 0.54)
    Save-Png $canvas[0] $canvas[1] (Join-Path $folder 'ic_launcher.png')

    $canvas = New-Canvas $size $size $true
    Fill-Background $canvas[1] $size $size $true
    Draw-Spider $canvas[1] ($size / 2) ($size * 0.51) ($size * 0.54)
    Save-Png $canvas[0] $canvas[1] (Join-Path $folder 'ic_launcher_round.png')

    $canvas = New-Canvas $size $size $true
    Draw-Spider $canvas[1] ($size / 2) ($size * 0.51) ($size * 0.58)
    Save-Png $canvas[0] $canvas[1] (Join-Path $folder 'ic_launcher_foreground.png')
}

$splashFiles = Get-ChildItem -LiteralPath $ResourceRoot -Recurse -Filter 'splash.png'
foreach ($file in $splashFiles) {
    $source = [System.Drawing.Image]::FromFile($file.FullName)
    $width = $source.Width
    $height = $source.Height
    $source.Dispose()

    $canvas = New-Canvas $width $height
    Fill-Background $canvas[1] $width $height
    $short = [Math]::Min($width, $height)
    $logoSize = [Math]::Min($short * 0.31, 210)
    $logoY = $height * 0.43
    Draw-Spider $canvas[1] ($width / 2) $logoY $logoSize

    $titleSize = [Math]::Max(14, [Math]::Min(32, $short * 0.045))
    $subSize = [Math]::Max(7, [Math]::Min(14, $short * 0.019))
    $titleFont = [System.Drawing.Font]::new('Microsoft YaHei UI', $titleSize, [System.Drawing.FontStyle]::Bold)
    $subFont = [System.Drawing.Font]::new('Segoe UI', $subSize, [System.Drawing.FontStyle]::Regular)
    $titleBrush = [System.Drawing.SolidBrush]::new($white)
    $subBrush = [System.Drawing.SolidBrush]::new($muted)
    $format = [System.Drawing.StringFormat]::new()
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $canvas[1].DrawString(
        $appTitle,
        $titleFont,
        $titleBrush,
        [System.Drawing.RectangleF]::new(0, $logoY + $logoSize * 0.47, $width, $titleSize * 1.55),
        $format
    )
    $canvas[1].DrawString(
        'SMART INSPECTION  /  ROBOT OPERATIONS',
        $subFont,
        $subBrush,
        [System.Drawing.RectangleF]::new(0, $logoY + $logoSize * 0.47 + $titleSize * 1.45, $width, $subSize * 1.7),
        $format
    )
    $titleFont.Dispose()
    $subFont.Dispose()
    $titleBrush.Dispose()
    $subBrush.Dispose()
    $format.Dispose()
    Save-Png $canvas[0] $canvas[1] $file.FullName
}

$preview = New-Canvas 512 512
Fill-Background $preview[1] 512 512
Draw-Spider $preview[1] 256 260 282
Save-Png $preview[0] $preview[1] $PreviewPath

Write-Host "Android branding generated in $ResourceRoot"
Write-Host "Preview: $PreviewPath"
