# يرسم مخطط التوصيل الكهربائي docs/images/wiring.png: خلفية بيضاء، القطع بشكلها الحقيقي، وكل النصوص بالعربي.
# المنافذ مطابقة لـ HyundaiCompanion/config.h.
#   powershell -ExecutionPolicy Bypass -File tools\gen_wiring.ps1
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot
$outFile = Join-Path $root 'docs\images\wiring.png'

$W = 2000; $H = 1400
$bmp = New-Object Drawing.Bitmap($W, $H)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$g.TextRenderingHint = 'AntiAliasGridFit'

# ================== أدوات الرسم ==================
function C($hex) { [Drawing.ColorTranslator]::FromHtml($hex) }
function Brush($hex) { New-Object Drawing.SolidBrush (C $hex) }
function PenOf($hex, $w) { $p = New-Object Drawing.Pen((C $hex), $w); $p.LineJoin = 'Round'; $p.StartCap = 'Round'; $p.EndCap = 'Round'; $p }
function Font($size, $bold = $false) { New-Object Drawing.Font('Segoe UI', $size, $(if ($bold) { 'Bold' } else { 'Regular' }), 'Pixel') }
function RoundRect($x, $y, $w, $h, $r) {
  $p = New-Object Drawing.Drawing2D.GraphicsPath
  $p.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90); $p.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
  $p.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90); $p.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
  $p.CloseFigure(); $p
}
function Box($x, $y, $w, $h, $fill, $edge, $r = 12) {
  $p = RoundRect $x $y $w $h $r
  $g.FillPath((Brush $fill), $p)
  if ($edge) { $g.DrawPath((PenOf $edge 2), $p) }
}
# ثقوب التثبيت في زوايا اللوحة
function Holes($x, $y, $w, $h) {
  foreach ($c in @(@(($x + 14), ($y + 14)), @(($x + $w - 14), ($y + 14)), @(($x + 14), ($y + $h - 14)), @(($x + $w - 14), ($y + $h - 14)))) {
    $g.FillEllipse((Brush '#F2F2F0'), $c[0] - 7, $c[1] - 7, 14, 14)
    $g.DrawEllipse((PenOf '#C8B27A' 2), $c[0] - 7, $c[1] - 7, 14, 14)
  }
}
# نص عربي/مختلط: Right = ينتهي عند x، Center = حول x، Left = يبدأ من x
function Ar($s, $x, $y, $size, $color, $bold = $false, $anchor = 'Center') {
  $sf = New-Object Drawing.StringFormat
  $sf.FormatFlags = [Drawing.StringFormatFlags]::DirectionRightToLeft
  $sf.Alignment = switch ($anchor) { 'Right' { 'Near' } 'Left' { 'Far' } default { 'Center' } }
  $g.DrawString($s, (Font $size $bold), (Brush $color), [float]$x, [float]$y, $sf)
}
# نص لاتيني صغير (أسماء المنافذ المطبوعة على اللوحات)
function Lt($s, $x, $y, $size, $color, $bold = $false, $align = 'Center') {
  $sf = New-Object Drawing.StringFormat; $sf.Alignment = $align
  $g.DrawString($s, (Font $size $bold), (Brush $color), [float]$x, [float]$y, $sf)
}
# عنوان القطعة: اسم عربي كبير وتحته الموديل باللون الرمادي
function Title($ar, $sub, $x, $y) {
  Ar $ar $x $y 28 '#1F2328' $true
  if ($sub) { Ar $sub $x ($y + 36) 18 '#6B7280' }
}
function PinPad($x, $y) {
  $g.FillRectangle((Brush '#2B2B2B'), $x - 9, $y - 9, 18, 18)
  $g.FillEllipse((Brush '#E3B341'), $x - 6, $y - 6, 12, 12)
}
function Dot($x, $y, $color) { $g.FillEllipse((Brush $color), $x - 7, $y - 7, 14, 14) }
function Wire($color, [int[]]$pts, $width = 5) {
  $arr = for ($i = 0; $i -lt $pts.Count; $i += 2) { New-Object Drawing.Point($pts[$i], $pts[$i + 1]) }
  $g.DrawLines((PenOf $color $width), [Drawing.Point[]]$arr)
}
# اسم السلك فوقه بخلفية بيضاء
function WireLabel($s, $x, $y, $color) {
  $f = Font 17 $true
  $sz = $g.MeasureString($s, $f)
  $g.FillRectangle((Brush '#FAFAF8'), [float]($x - $sz.Width / 2 - 4), [float]($y - 2), [float]($sz.Width + 8), [float]($sz.Height + 2))
  Ar $s $x $y 17 $color $true
}

# ================== الألوان ==================
$RED = '#D7282F'; $ORANGE = '#EE8A00'; $BLACK = '#2B2B2B'; $SDA = '#1E6FD9'; $SCL = '#D9A400'
$SIG = '#1F9D55'; $TXC = '#8E44AD'; $RXC = '#D6336C'

# ================== الخلفية والعنوان ==================
$g.Clear((C '#FAFAF8'))
Ar 'مخطط التوصيل الكهربائي' ($W / 2) 34 52 '#1F2328' $true
Ar 'مساعد السيارة الذكي  ·  Hyundai Smart Companion' ($W / 2) 104 22 '#6B7280'

# ================== المواضع ==================
$X5 = 560; $XG = 600                         # خطا التغذية 5V والأرضي
$XV = 1310; $XN = 1340; $XD = 1370; $XC = 1400   # 3.3V، أرضي، SDA، SCL
$EX = 640; $EY = 470; $EW = 630; $EH = 300   # لوحة ESP32
$TOPY = $EY + 22; $BOTY = $EY + $EH - 22     # صفا المنافذ
$RTCY = 200; $AHTY = 560; $HWY = 1000        # الوحدات على اليمين
$DFX = 200; $DFY = 890                       # DFPlayer

# منافذ ESP32 المستخدمة
$P3V3 = 1066; $PGNDT = 1103; $PSDA = 1140; $PSCL = 1177       # الصف العلوي
$P5V = 670; $PGNDB = 706; $P16 = 742; $P17 = 778; $P27 = 1174 # الصف السفلي

# ================== الأسلاك (تحت القطع) ==================
# التغذية من سلك USB المقطوع
Wire $RED   @(430, 236, $X5, 236, $X5, 930)
Wire $BLACK @(430, 262, $XG, 262, $XG, 1050)
Wire $RED   @($P5V, $BOTY, $P5V, 830, $X5, 830)
Wire $BLACK @($PGNDB, $BOTY, $PGNDB, 850, $XG, 850)
Wire $RED   @($X5, 930, ($DFX + 280), 930)
Wire $BLACK @($XG, 1050, ($DFX + 280), 1050)
Wire $RED   @($X5, 660, 572, 660) 4; Wire $BLACK @(588, 660, $XG, 660) 4

# DFPlayer ↔ ESP32
Wire $TXC @($P16, $BOTY, $P16, 970, ($DFX + 280), 970)
Wire $RXC @($P17, $BOTY, $P17, 1010, 745, 1010)
Wire $RXC @(635, 1010, ($DFX + 280), 1010)

# السماعة
Wire $RED   @(300, ($DFY + 190), 300, 1150)
Wire $BLACK @(360, ($DFY + 190), 360, 1150)

# 3.3V و I2C إلى اليمين
Wire $ORANGE @($P3V3, $TOPY, $P3V3, 445, $XV, 445)
Wire $BLACK  @($PGNDT, $TOPY, $PGNDT, 430, $XN, 430)
Wire $SDA    @($PSDA, $TOPY, $PSDA, 415, $XD, 415)
Wire $SCL    @($PSCL, $TOPY, $PSCL, 400, $XC, 400)
Wire $ORANGE @($XV, ($RTCY + 30), $XV, ($HWY + 30))
Wire $BLACK  @($XN, ($RTCY + 60), $XN, ($HWY + 60))
Wire $SDA    @($XD, ($RTCY + 90), $XD, ($AHTY + 90))
Wire $SCL    @($XC, ($RTCY + 120), $XC, ($AHTY + 120))
foreach ($y0 in $RTCY, $AHTY) {
  Wire $ORANGE @($XV, ($y0 + 30), 1450, ($y0 + 30));  Dot $XV ($y0 + 30) $ORANGE
  Wire $BLACK  @($XN, ($y0 + 60), 1450, ($y0 + 60));  Dot $XN ($y0 + 60) $BLACK
  Wire $SDA    @($XD, ($y0 + 90), 1450, ($y0 + 90));  Dot $XD ($y0 + 90) $SDA
  Wire $SCL    @($XC, ($y0 + 120), 1450, ($y0 + 120)); Dot $XC ($y0 + 120) $SCL
}
Wire $ORANGE @($XV, ($HWY + 30), 1450, ($HWY + 30)); Dot $XV ($HWY + 30) $ORANGE
Wire $BLACK  @($XN, ($HWY + 60), 1450, ($HWY + 60)); Dot $XN ($HWY + 60) $BLACK
Wire $SIG    @($P27, $BOTY, $P27, ($HWY + 90), 1450, ($HWY + 90))      # تحت نهايات الخطوط فلا يتقاطع
Dot $XV 445 $ORANGE; Dot $XN 430 $BLACK; Dot $XD 415 $SDA; Dot $XC 400 $SCL
Dot $X5 830 $RED; Dot $XG 850 $BLACK; Dot $X5 660 $RED; Dot $XG 660 $BLACK

# أسماء الأسلاك
WireLabel 'موجب 5 فولت' 470 200 $RED
WireLabel 'أرضي' 470 268 $BLACK
WireLabel '3.3 فولت' 1210 449 $ORANGE
WireLabel 'إرسال TX' 680 950 $TXC
WireLabel 'استقبال RX' 545 1014 $RXC
WireLabel 'إشارة الاهتزاز' 1300 ($HWY + 94) $SIG

# ================== شاحن USB للسيارة + السلك المقطوع ==================
Title 'شاحن USB للسيارة' '5 فولت  ·  في ولّاعة السيارة' 210 140
$g.FillPie((Brush '#B9BEC6'), 70, 214, 60, 70, 90, 180)
$g.FillRectangle((Brush '#B9BEC6'), 98, 214, 28, 70)
$g.FillEllipse((Brush '#8C929C'), 64, 240, 14, 18)
Box 120 204 170 90 '#2B2D33' $null 18
$g.FillRectangle((Brush '#D9DCE1'), 250, 232, 30, 34)
$g.FillRectangle((Brush '#4A90E2'), 256, 241, 18, 6)
$g.DrawLine((PenOf '#9AA0A8' 14), 290, 249, 410, 249)
$g.FillRectangle((Brush '#9AA0A8'), 404, 236, 18, 30)
Ar 'سلك USB مقطوع' 360 290 17 '#6B7280'

# مكثف (موصى به)
$g.FillRectangle((Brush '#1D4ED8'), 572, 640, 16, 40)
$g.FillRectangle((Brush '#DCE7FF'), 584, 640, 4, 40)
$g.FillEllipse((Brush '#9DB5F0'), 572, 636, 16, 8)
Ar 'مكثف 1000 ميكروفاراد' 545 630 18 '#1F2328' $true 'Right'
Ar 'موصى به لثبات الجهد' 545 656 16 '#6B7280' $false 'Right'

# ================== ESP32 مع الشاشة ==================
Title 'المعالج والشاشة' 'ESP32 مع شاشة OLED مدمجة' 905 384
Box $EX $EY $EW $EH '#1B1C20' '#3A3D45' 14
Holes $EX $EY $EW $EH
$ant = for ($i = 0; $i -le 6; $i++) { New-Object Drawing.Point((662 + $i * 12), $(if ($i % 2) { 492 } else { 504 })) }
$g.DrawLines((PenOf '#C8A951' 3), [Drawing.Point[]]$ant)
for ($i = 0; $i -lt 12; $i++) { PinPad (770 + $i * 37) $TOPY }
for ($i = 0; $i -lt 15; $i++) { PinPad (670 + $i * 36) $BOTY }
foreach ($p in @(@($P3V3, '3V3'), @($PGNDT, 'GND'), @($PSDA, 'G21'), @($PSCL, 'G22'))) { Lt $p[1] $p[0] ($TOPY + 12) 13 '#E6E6E6' $true }
foreach ($p in @(@($P5V, '5V'), @($PGNDB, 'GND'), @($P16, 'G16'), @($P17, 'G17'), @($P27, 'G27'))) { Lt $p[1] $p[0] ($BOTY - 30) 13 '#E6E6E6' $true }
Box 760 540 260 160 '#0A0B0D' '#444851' 6
$g.FillRectangle((Brush '#3A2F0A'), 772, 552, 236, 30)
$g.FillRectangle((Brush '#0C2236'), 772, 584, 236, 104)
Lt 'OLED 128×64' 890 556 16 '#FFD54A' $true
Lt '4:25' 890 592 58 '#5CC8FF' $true
Box 1050 555 170 140 '#C9CDD3' '#9AA0A8' 6
Lt 'ESP32' 1135 590 34 '#3A3D45' $true
Lt 'WROOM-32' 1135 636 16 '#555A63' $true
$g.FillRectangle((Brush '#C9CDD3'), ($EX + $EW - 8), 595, 26, 46)

# ================== قطعة OBD (بلوتوث) ==================
Title 'قطعة OBD' 'ELM327 v2.1  ·  في منفذ OBD بالسيارة' 860 150
$tz = New-Object Drawing.Drawing2D.GraphicsPath
$tz.AddPolygon([Drawing.Point[]]@((New-Object Drawing.Point(700, 236)), (New-Object Drawing.Point(740, 228)), (New-Object Drawing.Point(740, 300)), (New-Object Drawing.Point(700, 292))))
$g.FillPath((Brush '#3A3D45'), $tz)
Box 740 220 240 88 '#1E3A66' '#2E5A9C' 14
Lt 'ELM327' 860 236 30 '#FFFFFF' $true
$g.FillEllipse((Brush '#4ADE80'), 952, 232, 10, 10)
$bt = PenOf '#2F80ED' 3; $bt.DashStyle = 'Dash'
$g.DrawLine($bt, 760, 310, 690, 480)
foreach ($r in 12, 24, 36) { $g.DrawArc((PenOf '#2F80ED' 3), (686 - $r), (488 - $r), (2 * $r), (2 * $r), 220, 70) }
Ar 'بلوتوث بدون أسلاك' 768 340 18 '#2F80ED' $true 'Left'

# ================== الوحدات على اليمين ==================
function Module($y, $h, $fill, $edge, $ar, $sub, [string[]]$pins) {
  Title $ar $sub 1615 ($y - 82)
  Box 1450 $y 330 $h $fill $edge 12
  Holes 1450 $y 330 $h
  $g.FillRectangle((Brush '#2B2B2B'), 1436, ($y + 16), 24, ($pins.Count * 30 + 4))
  for ($i = 0; $i -lt $pins.Count; $i++) {
    $py = $y + 30 + $i * 30
    $g.FillEllipse((Brush '#E3B341'), 1442, $py - 6, 12, 12)
    Lt $pins[$i] 1468 ($py - 10) 15 '#FFFFFF' $true 'Near'
  }
}
Module $RTCY 170 '#1F4E9A' '#163B76' 'الساعة' 'PCF8563  ·  تحفظ الوقت ببطارية' @('VCC', 'GND', 'SDA', 'SCL')
$g.FillEllipse((Brush '#D9DCE1'), 1585, ($RTCY + 25), 120, 120)
$g.DrawEllipse((PenOf '#9AA0A8' 3), 1595, ($RTCY + 35), 100, 100)
Lt 'CR2032' 1645 ($RTCY + 73) 18 '#555A63' $true
Lt '+' 1645 ($RTCY + 40) 20 '#555A63' $true

Module $AHTY 170 '#5E2B8C' '#47206B' 'الحرارة والرطوبة والضغط' 'AHT20 + BMP280' @('VCC', 'GND', 'SDA', 'SCL')
Box 1570 ($AHTY + 45) 56 56 '#F3EEDF' '#C9C2AE' 4
Box 1660 ($AHTY + 50) 46 46 '#C9CDD3' '#9AA0A8' 4
$g.FillEllipse((Brush '#7C828C'), 1679, ($AHTY + 69), 8, 8)
Lt 'AHT20' 1598 ($AHTY + 108) 14 '#EADCFB' $true
Lt 'BMP280' 1683 ($AHTY + 108) 14 '#EADCFB' $true

Module $HWY 150 '#1D3B6E' '#142C54' 'حساس الاهتزاز' 'HW-072' @('VCC', 'GND', 'DO')
$g.FillPath((Brush '#C9CDD3'), (RoundRect 1560 ($HWY + 40) 110 34 17))
for ($x = 1572; $x -lt 1660; $x += 9) { $g.DrawLine((PenOf '#7C828C' 2), $x, ($HWY + 44), ($x + 5), ($HWY + 70)) }
Box 1690 ($HWY + 38) 40 40 '#2F6FD9' '#1E4FA8' 4
$g.FillEllipse((Brush '#F2F2F0'), 1700, ($HWY + 48), 20, 20)
$g.FillEllipse((Brush '#E53935'), 1600, ($HWY + 98), 10, 10)
$g.FillEllipse((Brush '#43A047'), 1625, ($HWY + 98), 10, 10)

# ================== DFPlayer والمقاومة والسماعة ==================
Title 'مشغل الصوت' 'DFPlayer Mini  ·  وبطاقة microSD' ($DFX + 140) ($DFY - 92)
Box $DFX $DFY 280 190 '#B3242E' '#8E1C24' 12
Holes $DFX $DFY 280 190
Box ($DFX + 40) ($DFY + 45) 130 95 '#D9DCE1' '#9AA0A8' 6
Box ($DFX + 52) ($DFY + 57) 106 71 '#2B2D33' $null 4
Lt 'microSD' ($DFX + 105) ($DFY + 80) 17 '#D9DCE1' $true
$g.FillRectangle((Brush '#2B2B2B'), ($DFX + 268), ($DFY + 26), 24, 144)
foreach ($p in @(@(930, 'VCC'), @(970, 'TX'), @(1010, 'RX'), @(1050, 'GND'))) {
  $g.FillEllipse((Brush '#E3B341'), ($DFX + 274), $p[0] - 6, 12, 12)
  Lt $p[1] ($DFX + 262) ($p[0] - 10) 15 '#FFFFFF' $true 'Far'
}
$g.FillRectangle((Brush '#2B2B2B'), 285, ($DFY + 178), 90, 24)
$g.FillEllipse((Brush '#E3B341'), 294, ($DFY + 184), 12, 12)
$g.FillEllipse((Brush '#E3B341'), 354, ($DFY + 184), 12, 12)
Lt 'SPK1' 300 ($DFY + 152) 13 '#FFFFFF' $true
Lt 'SPK2' 360 ($DFY + 152) 13 '#FFFFFF' $true

$g.FillPath((Brush '#E9D7AE'), (RoundRect 635 996 110 28 12))
foreach ($b in @(@(655, '#7A4A1E'), @(674, '#111111'), @(693, '#D62828'), @(722, '#C9A227'))) { $g.FillRectangle((Brush $b[1]), $b[0], 996, 9, 28) }
Ar 'مقاومة 1 كيلو أوم' 690 1032 17 '#1F2328' $true

$g.FillEllipse((Brush '#2B2D33'), 255, 1150, 150, 150)
$g.FillEllipse((Brush '#4A4E57'), 280, 1175, 100, 100)
$g.FillEllipse((Brush '#1B1C20'), 312, 1207, 36, 36)
Title 'السماعة' '3 واط  ·  4 إلى 8 أوم' 150 1185

# ================== دليل الألوان ==================
Box 60 1330 1880 56 '#F0F0EE' '#DADAD6' 14
$legend = @(
  @($RED, 'موجب 5 فولت'), @($BLACK, 'أرضي'), @($ORANGE, '3.3 فولت'), @($SDA, 'بيانات SDA'),
  @($SCL, 'ساعة SCL'), @($SIG, 'إشارة الاهتزاز'), @($TXC, 'إرسال TX'), @($RXC, 'استقبال RX'))
for ($i = 0; $i -lt $legend.Count; $i++) {
  $x = 1880 - $i * 228
  $g.DrawLine((PenOf $legend[$i][0] 7), ($x - 52), 1358, $x, 1358)
  Ar $legend[$i][1] ($x - 62) 1345 18 '#1F2328' $false 'Right'
}

$g.Dispose()
$bmp.Save($outFile, [Drawing.Imaging.ImageFormat]::Png)
"wrote $outFile"
