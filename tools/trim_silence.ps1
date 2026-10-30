# قص الصمت من ملفات الكلام (بدون إعادة ترميز) لملفات جديدة قبل نسخها للبطاقة.
# الاستخدام:  powershell -ExecutionPolicy Bypass -File tools\trim_silence.ps1 <مجلد_المصدر> <مجلد_الناتج>
# النغمات (0010-0012) تُنسخ كما هي.
param([Parameter(Mandatory)][string]$In, [Parameter(Mandatory)][string]$Out)

Add-Type -Path (Join-Path $PSScriptRoot 'Mp3Scan.cs')
New-Item -ItemType Directory -Force $Out | Out-Null
$tones = '0010', '0011', '0012'

foreach ($p in Get-ChildItem $In -Filter *.mp3 | Sort-Object Name) {
  $d = [IO.File]::ReadAllBytes($p.FullName)
  $dest = Join-Path $Out $p.Name
  if ($tones -contains $p.BaseName) { [IO.File]::WriteAllBytes($dest, $d); "$($p.BaseName) tone (unchanged)"; continue }
  $info = ''
  # 145 = حد الكلام (global_gain)، 1 إطار قبل الكلام، 4 إطارات (~100ms) بعده
  [IO.File]::WriteAllBytes($dest, [Mp3Scan]::Trim($d, 145, 1, 4, [ref]$info))
  "$($p.BaseName) $info"
}
