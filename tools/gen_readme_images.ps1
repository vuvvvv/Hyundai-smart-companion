# يولّد صور الشاشة لملف README في docs/images: نفس خطوط الجهاز وصوره وطريقة رسم display.cpp،
# فالصورة مطابقة لما يظهر على الشاشة الحقيقية (بيانات تجريبية).
#   powershell -ExecutionPolicy Bypass -File tools\gen_readme_images.ps1
Add-Type -AssemblyName System.Drawing
Add-Type -Path (Join-Path $PSScriptRoot 'OledSim.cs') -ReferencedAssemblies System.Drawing
$root = Split-Path $PSScriptRoot
$lib  = Join-Path $env:USERPROFILE 'Documents\Arduino\libraries\Adafruit_GFX_Library'
$out  = Join-Path $root 'docs\images'
New-Item -ItemType Directory -Force $out | Out-Null

function Hex-Bytes([string]$s) { [byte[]]([regex]::Matches($s, '0x[0-9A-Fa-f]{2}') | ForEach-Object { [Convert]::ToByte($_.Value, 16) }) }

# ---------- الخطوط ----------
$glcd = Get-Content (Join-Path $lib 'glcdfont.c') -Raw
$classic = Hex-Bytes ($glcd.Substring($glcd.IndexOf('font[]')))

function Load-GfxFont([string]$name) {
  $t = Get-Content (Join-Path $lib "Fonts\$name.h") -Raw
  $f = New-Object GfxFont
  $bm = [regex]::Match($t, "${name}Bitmaps\[\][^{]*\{([^}]*)\}").Groups[1].Value
  $f.Bitmap = Hex-Bytes $bm
  $gl = [regex]::Match($t, "${name}Glyphs\[\][^{]*\{(.*?)\};", 'Singleline').Groups[1].Value
  $i = 0
  foreach ($m in [regex]::Matches($gl, '\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}')) {
    $g = New-Object GfxGlyph
    $g.Offset = [int]$m.Groups[1].Value; $g.W = [int]$m.Groups[2].Value; $g.H = [int]$m.Groups[3].Value
    $g.XAdv = [int]$m.Groups[4].Value; $g.XOff = [int]$m.Groups[5].Value; $g.YOff = [int]$m.Groups[6].Value
    $f.Glyphs[0x20 + $i] = $g; $i++
  }
  return $f
}
$F18 = Load-GfxFont 'FreeSansBold18pt7b'
$F24 = Load-GfxFont 'FreeSansBold24pt7b'

# ---------- الصور النقطية من bitmaps.h ----------
$bh = Get-Content (Join-Path $root 'HyundaiCompanion\bitmaps.h') -Raw -Encoding UTF8
$B = @{}
foreach ($m in [regex]::Matches($bh, 'constexpr Bitmap (\w+) = \{(\d+), (\d+), (\w+)\}')) {
  $data = [regex]::Match($bh, "$($m.Groups[4].Value)\[\] = \{([^}]*)\}").Groups[1].Value
  $B[$m.Groups[1].Value] = @{ W = [int]$m.Groups[2].Value; H = [int]$m.Groups[3].Value; D = (Hex-Bytes $data) }
}

$o = New-Object OledSim(, $classic)
$DEG = [string][char]0xF8

# ---------- نفس دوال display.cpp ----------
function Img($name, $x, $y, $on = $true) { $b = $B[$name]; $o.Bitmap($b.D, $b.W, $b.H, $x, $y, $on) }
function BandY($name) { [math]::Floor((16 - $B[$name].H) / 2) }
function Small($s, $x, $y) { $o.Small($s, $x, $y, 1, $true) }
function SmallRight($s, $y, $right = 128) { Small $s ($right - $s.Length * 6) $y }
function BigCentered($s, $font, $baseline, $left, $areaW) {
  $x1 = 0; $w = 0; $o.Bounds($font, $s, [ref]$x1, [ref]$w)
  $o.Big($font, $s, ($left + [math]::Floor(($areaW - $w) / 2) - $x1), $baseline)
}
function InfoRight($icon, $text, $y) { $x = 128 - $text.Length * 6; Small $text $x $y; Img $icon ($x - $B[$icon].W - 2) $y }
function StatusIcons([string[]]$icons) {
  $x = 0
  foreach ($n in $icons) { if ($x + $B[$n].W -gt 46) { break }; Img $n $x (BandY $n); $x += $B[$n].W + 3 }
}
function Save($name) { $o.Render(6).Save((Join-Path $out "$name.png"), [Drawing.Imaging.ImageFormat]::Png); $o.Clear() }

# ---------- الشاشة العادية ----------
StatusIcons @('S_CAR')
Img 'DAY_1' (128 - $B['DAY_1'].W) (BandY 'DAY_1')                 # الاثنين
SmallRight '26/10' 4 (128 - $B['DAY_1'].W - 4)
BigCentered '4:25' $F18 42 0 112
Small 'PM' 114 19
Img 'N_HOUSE' 0 52
$o.Small('24.5', 11, 48, 2, $true); $o.Circle1($o.CursorX + 2, 49)
InfoRight 'N_COOLANT' "88${DEG}C" 48
InfoRight 'N_TREE' "29${DEG}C" 56
Save 'screen_normal'

# ---------- القيادة ----------
StatusIcons @('S_CAR')
SmallRight '4:25PM' 4
BigCentered '72' $F24 52 0 104
Small 'km/h' 104 44
Small '2150rpm' 0 56
InfoRight 'N_COOLANT' "90${DEG}C" 56
Save 'screen_drive'

# ---------- التنبيهات ----------
function Alert($title, $icon, $value, $unit, [bool]$strong, $bars = 0) {
  if ($strong) { $o.FillRect(0, 0, 128, 16, $true) }
  Img $title ([math]::Floor((128 - $B[$title].W) / 2)) (BandY $title) (-not $strong)
  Img $icon 2 (16 + [math]::Floor((48 - $B[$icon].H) / 2))
  $left = $B[$icon].W + 6
  if ($bars) {
    for ($i = 0; $i -lt 5; $i++) {
      $h = 8 + $i * 6; $x = $left + 6 + $i * 15; $y = 58 - $h
      if ($i -lt $bars) { $o.FillRect($x, $y, 11, $h, $true) } else { $o.Rect($x, $y, 11, $h) }
    }
    return
  }
  $areaW = 128 - $left; $x1 = 0; $w = 0
  $o.Bounds($F24, $value, [ref]$x1, [ref]$w)
  $font = if ($w -le $areaW - 4) { $F24 } else { $F18 }
  BigCentered $value $font 50 $left $areaW
  if ($unit) { SmallRight $unit 56 }
}
Alert 'T_COOLANT' 'I_COOLANT' '106' "${DEG}C" $true;  Save 'alert_coolant'
Alert 'T_SPEED'   'I_SPEED'   '83'  'km/h' $false;    Save 'alert_speed'
Alert 'T_VIBRATE' 'I_VIBRATE' ''    ''     $false 4;  Save 'alert_vibration'
Alert 'T_HOT'     'I_HOT'     '41.2' "${DEG}C" $false; Save 'alert_hot'
Alert 'T_COLD'    'I_COLD'    '13.5' "${DEG}C" $false; Save 'alert_cold'
Alert 'T_REST'    'I_REST'    '30'  'min'  $false;    Save 'alert_rest'

# ---------- الترحيب ----------
Img 'WORDMARK' ([math]::Floor((128 - $B['WORDMARK'].W) / 2)) (16 + [math]::Floor((48 - $B['WORDMARK'].H) / 2))
Save 'screen_splash'

# ---------- شريط الحالة: كل الرموز ----------
$x = 4
foreach ($n in 'S_CAR', 'S_MOON', 'S_CLOCK', 'S_WARN', 'S_SD', 'S_MUTE', 'S_CUP') { Img $n $x (BandY $n); $x += $B[$n].W + 6 }
Save 'status_icons'

"images written to $out"
