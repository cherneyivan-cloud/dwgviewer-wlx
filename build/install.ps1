# install.ps1 - copies dist into Total Commander and registers the plugin
# in wincmd.ini ([ListerPlugins] / [ListerPlugins64]).
# Total Commander must be closed while this runs.
$ErrorActionPreference = 'Stop'

$root  = Split-Path $PSScriptRoot -Parent
$src   = Join-Path $root 'dist'
# каталог Total Commander: передача аргументом, либо переменная окружения
$tc = if ($args.Count -gt 0) { $args[0] }
      elseif ($env:COMMANDER_PATH) { $env:COMMANDER_PATH }
      else { Write-Error 'Укажите путь к Total Commander первым аргументом или задайте переменную COMMANDER_PATH'
             exit 1 }
$ini = Join-Path $tc 'wincmd.ini'
if (-not (Test-Path $ini)) {
    Write-Error "wincmd.ini not found at $ini"
}
if (-not (Test-Path (Join-Path $src 'dwgviewer.wlx'))) {
    Write-Error 'dist\dwgviewer.wlx not found. Run build.bat first.'
}

$pluginDir = Join-Path $tc 'plugins\wlx\dwgviewer'
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item (Join-Path $src 'dwgviewer.wlx')  $pluginDir -Force
Copy-Item (Join-Path $src 'libredwg-0.dll') $pluginDir -Force
Write-Host "Plugin copied to $pluginDir"

# ---- read ini, preserving encoding and newline style ----
$bytes = [System.IO.File]::ReadAllBytes($ini)
if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB) {
    $enc = [System.Text.Encoding]::UTF8
} elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
    $enc = [System.Text.Encoding]::Unicode
} else {
    $enc = [System.Text.Encoding]::Default
}
$text = $enc.GetString($bytes)
$nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
$lines = [System.Collections.Generic.List[string]]($text -split "`r?`n")

# find the highest index used in [ListerPlugins]
$maxIdx = -1
$inLp = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i].Trim()
    if ($l -match '^\[ListerPlugins\]') { $inLp = $true; continue }
    if ($l -match '^\[') { if ($inLp) { $inLp = $false } }
    if ($inLp -and $l -match '^(\d+)\s*=') {
        $v = [int]$Matches[1]
        if ($v -gt $maxIdx) { $maxIdx = $v }
    }
}
$next = $maxIdx + 1

$rel      = '%COMMANDER_PATH%\plugins\wlx\dwgviewer\dwgviewer.wlx'
$entryLp  = "$next=$rel"
$entry64  = "$next=1"

# idempotency: refuse to add a second registration while one exists
$existing = $null
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^(\d+)=.*dwgviewer\.wlx') {
        $existing = $Matches[1]
        break
    }
}
if ($null -ne $existing) {
    Write-Host "Plugin already registered at index $existing. Use build\normalize.ps1 to clean up duplicates."
    exit 0
}

# --- insert into [ListerPlugins] right before the next section header ---
$insertLp = $lines.Count  # append at end
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i].Trim() -eq '[ListerPlugins]') {
        $inLp = $true
        continue
    }
    if ($inLp -and $lines[$i].Trim() -match '^\[') {
        $insertLp = $i
        $inLp = $false
        break
    }
}
$lines.Insert($insertLp, $entryLp)

# --- insert into [ListerPlugins64] (after its last entry / before next section) ---
$ins64 = $lines.Count
$start64 = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i].Trim() -eq '[ListerPlugins64]') {
        $start64 = $i
        $in64 = $true
    } elseif ($start64 -ge 0 -and $in64 -and $lines[$i].Trim() -match '^\[') {
        $ins64 = $i
        break
    }
}
if ($start64 -ge 0) {
    # place after the last N=xx entry of the 64-bit section
    $lastEntry = $start64
    for ($i = $start64 + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -match '^\d+\s*=') {
            $lastEntry = $i
        } elseif ($lines[$i].Trim() -match '^\[|^$') {
            break
        }
    }
    if ($lastEntry -gt $start64) {
        $lines.Insert($lastEntry + 1, $entry64)
    } else {
        $lines.Insert($start64 + 1, $entry64)
    }
} else {
    $lines.Add($entry64)
}

# drop stale checksums so Total Commander recomputes them
$cleaned = [System.Collections.Generic.List[string]]::new()
foreach ($l in $lines) {
    if ($l.Trim() -match '^\$checksum\$=') { continue }
    $cleaned.Add($l)
}

$out = ($cleaned -join $nl) + $nl
[System.IO.File]::WriteAllText($ini, $out, $enc)
Write-Host "wincmd.ini updated: $entryLp / $entry64"
Write-Host 'Done. Start Total Commander and press F3 on a .dwg file.'