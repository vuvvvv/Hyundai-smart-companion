# يبني ملفات جُمل كاملة مدمجة مسبقاً من الملفات الأصلية في SD_card/mp3_untrimmed،
# ويضعها في SD_card/mp3 بجانب الملفات المفردة، ويضيفها لجدول الأصوات.
# الدمج = وصل إطارات MP3 مباشرة (كل ملفات الكلام بنفس الصيغة)، بلا إعادة ترميز.
# كل انتقال بين ملفين على DFPlayer فيه توقف، فكلما قلّ عدد الملفات في الجملة كان الكلام أسلس.
#   powershell -ExecutionPolicy Bypass -File tools\build_phrases.ps1
Add-Type -Path (Join-Path $PSScriptRoot 'Mp3Scan.cs')
$root = Split-Path $PSScriptRoot
$src  = Join-Path $root 'SD_card\mp3_untrimmed'
$dst  = Join-Path $root 'SD_card\mp3'
$csv  = Join-Path $root 'SD_card\جدول_الأصوات.csv'

# الأجزاء الداخلية بهامش 2 إطار (~50ms) والجزء الأخير 4 إطارات (~100ms)
$cache = @{}
function Part([int]$id, [int]$tail) {
  $key = "$id/$tail"
  if (-not $cache.ContainsKey($key)) {
    $d = [IO.File]::ReadAllBytes((Join-Path $src ('{0:D4}.mp3' -f $id)))
    $info = ''
    $cache[$key] = [Mp3Scan]::Trim($d, 145, 1, $tail, [ref]$info)
  }
  return ,$cache[$key]
}

$rows = New-Object System.Collections.Generic.List[string]
function Build([int]$id, [int[]]$parts, [string]$label) {
  $ms = New-Object IO.MemoryStream
  for ($i = 0; $i -lt $parts.Count; $i++) {
    $tail = if ($i -eq $parts.Count - 1) { 4 } else { 2 }
    $b = Part $parts[$i] $tail
    $ms.Write($b, 0, $b.Length)
  }
  $name = '{0:D4}.mp3' -f $id
  [IO.File]::WriteAllBytes((Join-Path $dst $name), $ms.ToArray())
  $joined = ($parts | ForEach-Object { '{0:D4}' -f $_ }) -join '+'
  $rows.Add(('{0:D4},{1},جملة مدمجة,{2} ({3})' -f $id, $name, $label, $joined))
}

# حذف أي جمل مدمجة سابقة (الأرقام 400 فما فوق)
Get-ChildItem $dst -Filter *.mp3 | Where-Object { [int]$_.BaseName -ge 400 } | Remove-Item

# ---------- الترحيب: 401 صباح الخير / 402 مساء الخير + يا + الاسم ----------
Build 401 @(1, 3, 4) 'صباح الخير يا عبد العزيز'
Build 402 @(2, 3, 4) 'مساء الخير يا عبد العزيز'

# DFPlayer يفهرس البطاقة كلها عند كل تشغيل: 1845 ملفاً منعته من الإقلاع، و332 عملت.
# لذلك الوقت ملفان والتاريخ 3 ملفات (المجموع ~368 ملفاً).

# ---------- الوقت: "الساعة الثالثة" (الملف المفرد 81..92) + "و خمس دقائق صباحاً/مساءً" ----------
# 600 + الدقيقة (صباحاً) / 700 + الدقيقة (مساءً)   → 601..659 / 701..759
for ($m = 1; $m -le 59; $m++) {
  Build (600 + $m) @(54, (100 + $m), 5) 'و + دقائق + صباحاً'
  Build (700 + $m) @(54, (100 + $m), 6) 'و + دقائق + مساءً'
}
# رأس الساعة في ملف واحد: "الساعة الثالثة صباحاً/مساءً"   → 881..892 / 981..992
for ($h = 1; $h -le 12; $h++) {
  Build (880 + $h) @((80 + $h), 5) 'الساعة + صباحاً'
  Build (980 + $h) @((80 + $h), 6) 'الساعة + مساءً'
}

# ---------- التاريخ ----------
# "اليوم هو الجمعة": 440 + يوم_الأسبوع (0 = الأحد)   → 440..446
for ($w = 0; $w -le 6; $w++) { Build (440 + $w) @(51, (40 + $w)) 'اليوم هو + يوم الأسبوع' }
# "تاريخ اليوم هو الحادي والعشرون": 500 + اليوم   → 501..531
for ($d = 1; $d -le 31; $d++) { Build (500 + $d) @(52, (300 + $d)) 'تاريخ اليوم هو + اليوم' }
# "من أكتوبر 2026": 800 + (السنة-2026)*12 + الشهر   → 801..836
for ($y = 0; $y -le 2; $y++) {
  for ($m = 1; $m -le 12; $m++) { Build (800 + $y * 12 + $m) @(53, (60 + $m), (226 + $y)) 'من + الشهر + السنة' }
}
# "من أكتوبر" لسنة غير مسجلة: 560 + الشهر   → 561..572
for ($m = 1; $m -le 12; $m++) { Build (560 + $m) @(53, (60 + $m)) 'من + الشهر' }

# جدول الأصوات: حذف صفوف الجمل القديمة ثم إضافة الجديدة
$utf8 = New-Object Text.UTF8Encoding $false
$keep = [IO.File]::ReadAllLines($csv, $utf8) | Where-Object { $_ -notmatch ',جملة مدمجة,' }
[IO.File]::WriteAllLines($csv, [string[]](@($keep) + $rows), $utf8)
"built $($rows.Count) phrase files"
