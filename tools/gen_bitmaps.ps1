# يولّد HyundaiCompanion/bitmaps.h: النصوص العربية والرموز والشعار كصور نقطية 1-بت للشاشة،
# وصورة معاينة tools/bitmaps_preview.png للتأكد من الشكل قبل الرفع.
# الخط العربي يُرسم بمحرك ويندوز (يشكّل الحروف ويصلها صحيحاً)، والرموز من Segoe UI Symbol.
#   powershell -ExecutionPolicy Bypass -File tools\gen_bitmaps.ps1
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot
$outH = Join-Path $root 'HyundaiCompanion\bitmaps.h'
$outPng = Join-Path $PSScriptRoot 'bitmaps_preview.png'

$AR_FONT  = 'Segoe UI'
$SYM_FONT = 'Segoe UI Symbol'
$items = New-Object System.Collections.Generic.List[object]

# ---------------------------------------------------------------
function New-Canvas([int]$w, [int]$h) {
  $b = New-Object Drawing.Bitmap($w, $h)
  $g = [Drawing.Graphics]::FromImage($b)
  $g.Clear([Drawing.Color]::Black)
  $g.TextRenderingHint = [Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
  $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::None
  return @($b, $g)
}

# أصغر مستطيل يحوي البكسلات المضاءة
function Get-Box($b) {
  $x0 = $b.Width; $y0 = $b.Height; $x1 = -1; $y1 = -1
  for ($y = 0; $y -lt $b.Height; $y++) {
    for ($x = 0; $x -lt $b.Width; $x++) {
      if ($b.GetPixel($x, $y).R -gt 127) {
        if ($x -lt $x0) { $x0 = $x }; if ($x -gt $x1) { $x1 = $x }
        if ($y -lt $y0) { $y0 = $y }; if ($y -gt $y1) { $y1 = $y }
      }
    }
  }
  if ($x1 -lt 0) { return $null }
  return @($x0, $y0, ($x1 - $x0 + 1), ($y1 - $y0 + 1))
}

function Add-Item([string]$name, $b, $box) {
  $items.Add([pscustomobject]@{ Name = $name; Bmp = $b; Box = $box })
}

# نص عربي بأكبر حجم يدخل في الارتفاع والعرض المعطيين
function Add-Text([string]$name, [string]$text, [int]$maxW, [int]$maxH, [int]$startPx) {
  for ($px = $startPx; $px -ge 8; $px--) {
    $c = New-Canvas 260 60; $b = $c[0]; $g = $c[1]
    $f = New-Object Drawing.Font($AR_FONT, $px, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $sf = New-Object Drawing.StringFormat
    $sf.FormatFlags = [Drawing.StringFormatFlags]::DirectionRightToLeft
    $g.DrawString($text, $f, [Drawing.Brushes]::White, (New-Object Drawing.RectangleF(0, 4, 256, 56)), $sf)
    $g.Dispose()
    $box = Get-Box $b
    if ($box -and $box[2] -le $maxW -and $box[3] -le $maxH) { Add-Item $name $b $box; return }
  }
  throw "text '$name' does not fit"
}

# رمز من خط الرموز بأكبر حجم يدخل في المقاس
function Add-Glyph([string]$name, [int]$code, [int]$maxW, [int]$maxH) {
  for ($px = 48; $px -ge 6; $px--) {
    $c = New-Canvas 80 80; $b = $c[0]; $g = $c[1]
    $f = New-Object Drawing.Font($SYM_FONT, $px, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
    $g.DrawString([char]::ConvertFromUtf32($code), $f, [Drawing.Brushes]::White, 8, 8)
    $g.Dispose()
    $box = Get-Box $b
    if ($box -and $box[2] -le $maxW -and $box[3] -le $maxH) { Add-Item $name $b $box; return }
  }
  throw "glyph '$name' does not fit"
}

# رسم يدوي: '#' = بكسل مضاء
function Add-Art([string]$name, [string[]]$rows) {
  $w = ($rows | Measure-Object -Property Length -Maximum).Maximum
  $c = New-Canvas $w $rows.Count; $b = $c[0]; $c[1].Dispose()
  for ($y = 0; $y -lt $rows.Count; $y++) {
    for ($x = 0; $x -lt $rows[$y].Length; $x++) {
      if ($rows[$y][$x] -eq '#') { $b.SetPixel($x, $y, [Drawing.Color]::White) }
    }
  }
  Add-Item $name $b @(0, 0, $w, $rows.Count)
}

# ---------------------------------------------------------------
# أيام الأسبوع (0 = الأحد مثل RTClib) — الشريط الأصفر ارتفاعه 16
$days = 'الأحد', 'الاثنين', 'الثلاثاء', 'الأربعاء', 'الخميس', 'الجمعة', 'السبت'
for ($i = 0; $i -lt 7; $i++) { Add-Text "DAY_$i" $days[$i] 48 14 15 }

# عناوين التنبيهات (الشريط الأصفر)
Add-Text 'T_COOLANT'  'حرارة المحرك'    124 14 15
Add-Text 'T_SPEED'    'تجاوز السرعة'    124 14 15
Add-Text 'T_VIBRATE'  'اهتزاز قوي'      124 14 15
Add-Text 'T_HOT'      'المقصورة حارة'   124 14 15
Add-Text 'T_COLD'     'المقصورة باردة'  124 14 15
Add-Text 'T_REST'     'خذ استراحة'      124 14 15

# الترحيب: كلمة HYUNDAI بخط عريض مائل، بأكبر حجم يدخل في المنطقة الزرقاء
function Add-Latin([string]$name, [string]$text, [string]$font, [Drawing.FontStyle]$style, [int]$maxW, [int]$maxH) {
  for ($px = 40; $px -ge 8; $px--) {
    $c = New-Canvas 260 70; $b = $c[0]; $g = $c[1]
    $f = New-Object Drawing.Font($font, $px, $style, [Drawing.GraphicsUnit]::Pixel)
    $g.DrawString($text, $f, [Drawing.Brushes]::White, 4, 4)
    $g.Dispose()
    $box = Get-Box $b
    if ($box -and $box[2] -le $maxW -and $box[3] -le $maxH) { Add-Item $name $b $box; return }
  }
  throw "text '$name' does not fit"
}
Add-Latin 'WORDMARK' 'HYUNDAI' 'Segoe UI' ([Drawing.FontStyle]::Bold -bor [Drawing.FontStyle]::Italic) 124 26

# رموز التنبيهات الكبيرة (المنطقة الزرقاء ارتفاعها 48)
Add-Glyph 'I_REST'    0x2615  34 32   # كوب قهوة
Add-Glyph 'I_COLD'    0x2744  34 32   # ثلج
Add-Glyph 'I_HOT'     0x2600  34 32   # شمس
Add-Glyph 'I_VIBRATE' 0x1F4F3 34 32   # اهتزاز
Add-Glyph 'I_SPEED'   0x23F2  34 32   # عداد

# حرارة الماء: ميزان حرارة فوق أمواج
$c = New-Canvas 80 80; $b = $c[0]; $g = $c[1]
$f = New-Object Drawing.Font($SYM_FONT, 25, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
$g.DrawString([char]::ConvertFromUtf32(0x1F321), $f, [Drawing.Brushes]::White, 20, 2)
$tb = Get-Box $b
$pen = New-Object Drawing.Pen([Drawing.Color]::White, 2)
$cx = $tb[0] + [int]($tb[2] / 2)
foreach ($dy in 3, 8) {
  $y = $tb[1] + $tb[3] + $dy
  $pts = for ($x = $cx - 15; $x -le $cx + 15; $x += 5) { New-Object Drawing.Point($x, ($y + $(if ((($x - $cx + 15) / 5) % 2) { -2 } else { 2 }))) }
  $g.DrawLines($pen, [Drawing.Point[]]$pts)
}
$g.Dispose()
Add-Item 'I_COOLANT' $b (Get-Box $b)

# رموز صغيرة: شريط الحالة (ارتفاع ≤ 10) وأسطر المعلومات (ارتفاع ≤ 8)
Add-Glyph 'S_CAR'   0x1F697 14 10
Add-Glyph 'S_MOON'  0x1F319 10 10
Add-Glyph 'S_CLOCK' 0x1F552 10 10
Add-Glyph 'S_WARN'  0x26A0  11 10
Add-Glyph 'S_CUP'   0x2615  10 10
Add-Glyph 'S_MUTE'  0x1F507 12 10
Add-Art 'S_SD' @(
  '.######.',
  '.#.#.###',
  '.#.#.#.#',
  '.#.....#',
  '.#.....#',
  '.#.....#',
  '.#.....#',
  '.#######')
Add-Glyph 'N_HOUSE' 0x1F3E0 9 8
Add-Art 'N_TREE' @(
  '..###...',
  '.#####..',
  '#######.',
  '.#####..',
  '..###...',
  '...#....',
  '...#....',
  '..###...')
Add-Glyph 'N_DROP'  0x1F4A7 8 8
Add-Art 'N_COOLANT' @(
  '...#....',
  '...##...',
  '...#....',
  '...##...',
  '..###...',
  '........',
  '.##..##.',
  '#..##..#')
Add-Art 'N_GAUGE' @(
  '..####..',
  '.#....#.',
  '#.....##',
  '#....#.#',
  '#...#..#',
  '#..##..#',
  '.#....#.',
  '..####..')

# ---------------------------------------------------------------
# الكتابة: صفوف بايتات، البت الأعلى = البكسل الأيسر (صيغة Adafruit drawBitmap)
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine('// ===== bitmaps.h — مُولّد تلقائياً بواسطة tools/gen_bitmaps.ps1، لا تعدله يدوياً =====')
[void]$sb.AppendLine('// النصوص العربية والرموز والشعار كصور نقطية 1-بت (صيغة Adafruit drawBitmap).')
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine('#include <stdint.h>')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('struct Bitmap { uint8_t w, h; const uint8_t* data; };')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('namespace bmp {')
$total = 0
foreach ($it in $items) {
  $x0, $y0, $w, $h = $it.Box
  $bpr = [math]::Ceiling($w / 8)
  $bytes = New-Object System.Collections.Generic.List[string]
  for ($y = 0; $y -lt $h; $y++) {
    for ($bx = 0; $bx -lt $bpr; $bx++) {
      $v = 0
      for ($k = 0; $k -lt 8; $k++) {
        $x = $bx * 8 + $k
        if ($x -lt $w -and $it.Bmp.GetPixel($x0 + $x, $y0 + $y).R -gt 127) { $v = $v -bor (0x80 -shr $k) }
      }
      $bytes.Add(('0x{0:X2}' -f $v))
    }
  }
  $total += $bytes.Count
  [void]$sb.AppendLine("static const uint8_t $($it.Name)_DATA[] = {$($bytes -join ',')};")
  [void]$sb.AppendLine("constexpr Bitmap $($it.Name) = {$w, $h, $($it.Name)_DATA};")
}
[void]$sb.AppendLine('constexpr Bitmap DAY[7] = {DAY_0, DAY_1, DAY_2, DAY_3, DAY_4, DAY_5, DAY_6};')
[void]$sb.AppendLine('}  // namespace bmp')
[IO.File]::WriteAllText($outH, $sb.ToString(), (New-Object Text.UTF8Encoding $false))

# ---------------------------------------------------------------
# المعاينة: كل صورة بحجمها الحقيقي مكبرة ×4 على خلفية زرقاء
$scale = 4; $pad = 6
$cols = 4; $cellW = 130; $cellH = 50
$rowsN = [math]::Ceiling($items.Count / $cols)
$sheet = New-Object Drawing.Bitmap(($cols * $cellW * $scale), ($rowsN * $cellH * $scale))
$gs = [Drawing.Graphics]::FromImage($sheet)
$gs.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$gs.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
$gs.Clear([Drawing.Color]::FromArgb(0, 0, 60))
$lab = New-Object Drawing.Font('Arial', 12)
for ($i = 0; $i -lt $items.Count; $i++) {
  $it = $items[$i]; $x0, $y0, $w, $h = $it.Box
  $cx = ($i % $cols) * $cellW * $scale; $cy = [math]::Floor($i / $cols) * $cellH * $scale
  $crop = $it.Bmp.Clone((New-Object Drawing.Rectangle($x0, $y0, $w, $h)), $it.Bmp.PixelFormat)
  $gs.FillRectangle([Drawing.Brushes]::Black, $cx + $pad, $cy + 20, $w * $scale, $h * $scale)
  $gs.DrawImage($crop, $cx + $pad, $cy + 20, $w * $scale, $h * $scale)
  $gs.DrawString("$($it.Name) ${w}x$h", $lab, [Drawing.Brushes]::Yellow, $cx + $pad, $cy + 2)
}
$gs.Dispose()
$sheet.Save($outPng, [Drawing.Imaging.ImageFormat]::Png)
"wrote $outH ($($items.Count) bitmaps, $total bytes) and preview"
