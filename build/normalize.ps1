# normalize.ps1 - register the DWG viewer plugin in wincmd.ini exactly once.
# Removes all previous dwgviewer entries (path lines, N=1 flags, _detect),
# then adds one fresh registration with the next free index.
$ErrorActionPreference = 'Stop'
$ini = if ($args.Count -gt 0) { $args[0] } else { 'C:\Program Files\totalcmd\wincmd.ini' }
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

# 1) collect the indices of any previous dwgviewer registrations
$affected = @()
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^(\d+)=.*dwgviewer\.wlx') {
        $affected += $Matches[1]
    }
}

# 2) compute the next free index in [ListerPlugins]
$maxIdx = -1
$inLp = $false
$sec64Start = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i].Trim()
    if ($l -eq '[ListerPlugins]') { $inLp = $true; continue }
    if ($l -eq '[ListerPlugins64]') { $sec64Start = $i; $inLp = $false; continue }
    if ($l -match '^\[') { $inLp = $false }
    if ($inLp -and $l -match '^(\d+)\s*=') {
        $v = [int]$Matches[1]
        if ($v -gt $maxIdx) { $maxIdx = $v }
    }
}
$next = $maxIdx + 1

# 3) rebuild the line list
$out = [System.Collections.Generic.List[string]]::new()
$in64 = $false
$needLp = $true
for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i]
    $t = $l.Trim()

    if ($t -eq '[ListerPlugins64]') {
        # insert the plugin path right before it if not yet inserted
        if ($needLp) {
            $out.Add("$next=%COMMANDER_PATH%\plugins\wlx\dwgviewer\dwgviewer.wlx")
            $needLp = $false
        }
        $in64 = $true
        $out.Add($l)
        continue
    }
    if ($t -match '^\[') { $in64 = $false }

    # drop everything belonging to earlier dwgviewer registrations
    if ($t -match '^(\d+)=.*dwgviewer\.wlx') { continue }
    if ($in64 -and $t -match '^(\d+)=1$' -and ($affected -contains $Matches[1])) {
        continue
    }
    if ($t -match '^(\d+)_detect=' -and ($affected -contains $Matches[1])) {
        continue
    }
    $out.Add($l)
}

# 4) ensure the N=1 flag inside [ListerPlugins64]
$has64 = $false
for ($i = 0; $i -lt $out.Count; $i++) {
    if ($out[$i].Trim() -match '^\[ListerPlugins64\]') { $in64 = $true; continue }
    if ($out[$i].Trim() -match '^\[') { $in64 = $false }
    if ($in64 -and $out[$i].Trim() -eq "$next=1") { $has64 = $true }
}
if (-not $has64) {
    $out.Add("$next=1")
}

# 5) drop stale checksums so TC recomputes them
$final = [System.Collections.Generic.List[string]]::new()
foreach ($l in $out) {
    if ($l.Trim() -match '^\$checksum\$=') { continue }
    $final.Add($l)
}

[System.IO.File]::WriteAllText($ini, ($final -join $nl) + $nl, $enc)
Write-Host ("Registered once at index {0}: {1} and {2}" -f $next, $entryLp, $entry64)