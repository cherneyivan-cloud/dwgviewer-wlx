# ============================================================================
#  DwgViewer WLX plugin installer for Total Commander
#
#  Works without administrator rights, and even when Total Commander is
#  installed into a folder where a normal user cannot create a "plugins"
#  subfolder (for example C:\Program Files\totalcmd). In that case the plugin
#  files are placed into the user profile and registered by absolute path --
#  Total Commander loads Lister plugins from any folder.
#
#  Usage:
#     install.bat                                  (double click)
#     powershell -ExecutionPolicy Bypass -File install.ps1
#     ... -TcDir "D:\TC"                  explicit Total Commander folder
#     ... -IniPath "C:\...\wincmd.ini"    explicit configuration file
#     ... -UserPluginDir "D:\Plugins\DwgViewer"
#     ... -FixIni                         move a read-only wincmd.ini to the profile
#     ... -NoIniUpdate                    only copy the files
#     ... -Update                         upgrade: close Total Commander first
#     ... -Uninstall                      remove the plugin and its registration
# ============================================================================
[CmdletBinding()]
param(
    [string]$TcDir,
    [string]$IniPath,
    [string]$UserPluginDir,
    [switch]$NoIniUpdate,
    [switch]$Update,
    [switch]$Uninstall,
    [switch]$FixIni,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$script_dir = Split-Path -Parent $MyInvocation.MyCommand.Path

$plugin_name   = 'DwgViewer'
$plugin_dll    = 'dwgviewer.wlx'
# Total Commander x64 loads the "*64" lister name when that file exists, so
# both names must always contain the same build.
$plugin_dll64  = 'dwgviewer.wlx64'
$runtime_dll   = 'libredwg-0.dll'
$ini_section   = 'ListerPlugins'
$ini_section64 = 'ListerPlugins64'
$reg_key       = 'HKCU:\Software\Ghisler\Total Commander'

function Write-Head($t) { Write-Host "`n$t" -ForegroundColor Cyan }
function Write-Ok($t)   { Write-Host "  [OK]   $t" -ForegroundColor Green }
function Write-Warn2($t){ Write-Host "  [WARN] $t" -ForegroundColor Yellow }
function Write-Err($t)  { Write-Host "  [FAIL] $t" -ForegroundColor Red }
function Write-Note($t) { Write-Host "         $t" -ForegroundColor Gray }

# ---------------------------------------------------------------------------
#  Locating Total Commander
# ---------------------------------------------------------------------------
function Test-TcFolder([string]$dir) {
    if (-not $dir) { return $false }
    foreach ($exe in 'totalcmd64.exe', 'totalcmd.exe') {
        if (Test-Path -LiteralPath (Join-Path $dir $exe) -PathType Leaf) { return $true }
    }
    return $false
}

function Get-RegistryValue([string]$name) {
    foreach ($key in @($reg_key, 'HKLM:\SOFTWARE\Ghisler\Total Commander',
                       'HKLM:\SOFTWARE\WOW6432Node\Ghisler\Total Commander')) {
        try {
            $item = Get-ItemProperty -Path $key -Name $name -ErrorAction Stop
            if ($item.$name) { return [string]$item.$name }
        } catch { }
    }
    return $null
}

function Find-TotalCommander {
    $candidates = @()
    $installDir = Get-RegistryValue 'InstallDir'
    if ($installDir) { $candidates += $installDir }
    if ($env:COMMANDER_PATH) { $candidates += $env:COMMANDER_PATH }
    $candidates += @(
        'C:\Program Files\totalcmd', 'C:\Program Files\totalcmd64',
        'C:\Program Files (x86)\totalcmd', 'C:\totalcmd', 'C:\TC',
        (Join-Path $env:LOCALAPPDATA 'totalcmd'), (Join-Path $env:APPDATA 'totalcmd')
    )
    foreach ($path in $candidates) {
        if (-not $path) { continue }
        if (Test-TcFolder $path) { return (Resolve-Path -LiteralPath $path).Path }
    }
    return $null
}

# ---------------------------------------------------------------------------
#  Locating wincmd.ini -- the registry value wins, TC reads the INI from there
# ---------------------------------------------------------------------------
function Resolve-IniFile([string]$tc) {
    $fromReg = Get-RegistryValue 'IniFileName'
    if ($fromReg -and (Test-Path -LiteralPath $fromReg -PathType Leaf)) {
        return [pscustomobject]@{ Path = $fromReg; Source = 'registry' }
    }
    $inProgramDir = Join-Path $tc 'wincmd.ini'
    if (Test-Path -LiteralPath $inProgramDir -PathType Leaf) {
        return [pscustomobject]@{ Path = $inProgramDir; Source = 'program dir' }
    }
    $inProfile = Join-Path $env:APPDATA 'Ghisler\wincmd.ini'
    if (Test-Path -LiteralPath $inProfile -PathType Leaf) {
        return [pscustomobject]@{ Path = $inProfile; Source = 'user profile' }
    }
    return [pscustomobject]@{ Path = $inProgramDir; Source = 'missing' }
}

# ---------------------------------------------------------------------------
#  Writability helpers
# ---------------------------------------------------------------------------
function Test-DirWritable([string]$dir) {
    try {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { return $false }
        $probe = Join-Path $dir ([guid]::NewGuid().ToString('N') + '.tmp')
        [System.IO.File]::WriteAllText($probe, 'x')
        Remove-Item -LiteralPath $probe -Force
        return $true
    } catch { return $false }
}

function Test-FileWritable([string]$path) {
    try {
        $fs = [System.IO.File]::Open($path, 'Open', 'ReadWrite', 'None')
        $fs.Close()
        return $true
    } catch { return $false }
}

# Can we create "plugins\wlx\DwgViewer" under the TC folder?
function Test-CanInstallInto([string]$tc) {
    $probeDir = Join-Path $tc ('plugins\_probe_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    try {
        New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $probeDir 'x.tmp'), 'x')
        Remove-Item -LiteralPath $probeDir -Recurse -Force
        return $true
    } catch {
        if (Test-Path -LiteralPath $probeDir) {
            try { Remove-Item -LiteralPath $probeDir -Recurse -Force } catch { }
        }
        return $false
    }
}

# ---------------------------------------------------------------------------
#  INI helpers (encoding and line endings are preserved)
# ---------------------------------------------------------------------------
function Read-IniFile([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $bomLen = 0
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $encoding = New-Object System.Text.UTF8Encoding($true); $bomLen = 3
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        $encoding = New-Object System.Text.UnicodeEncoding($false, $true); $bomLen = 2
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        $encoding = New-Object System.Text.UnicodeEncoding($true, $true); $bomLen = 2
    } else {
        $encoding = [System.Text.Encoding]::Default
    }
    $text = $encoding.GetString($bytes, $bomLen, $bytes.Length - $bomLen)

    $eol = "`r`n"
    if ($text -notmatch "`r`n" -and $text -match "`n") { $eol = "`n" }

    $preamble = [byte[]]@()
    if ($bomLen -gt 0) { $preamble = [byte[]]$encoding.GetPreamble() }

    [pscustomobject]@{
        Text     = $text
        Encoding = $encoding
        Preamble = $preamble
        Eol      = $eol
    }
}

function Write-IniFile($ini, [string]$path) {
    $body = $ini.Encoding.GetBytes([string]$ini.Text)
    $preamble = $ini.Preamble
    if ($null -eq $preamble) { $preamble = [byte[]]@() }
    $ms = New-Object System.IO.MemoryStream
    try {
        if ($preamble.Length -gt 0) { $ms.Write($preamble, 0, $preamble.Length) }
        if ($body.Length -gt 0)     { $ms.Write($body, 0, $body.Length) }
        [System.IO.File]::WriteAllBytes($path, $ms.ToArray())
    } finally { $ms.Dispose() }
}

function Get-IniSections([string]$text) {
    $result = New-Object System.Collections.ArrayList
    $current = [pscustomobject]@{ Name = ''; Header = $null; Lines = (New-Object System.Collections.ArrayList) }
    foreach ($line in ($text -split "`r`n|`n")) {
        if ($line -match '^\s*\[([^\]]+)\]\s*$') {
            [void]$result.Add($current)
            $current = [pscustomobject]@{ Name = $matches[1]; Header = $line; Lines = (New-Object System.Collections.ArrayList) }
        } else {
            [void]$current.Lines.Add($line)
        }
    }
    [void]$result.Add($current)
    return ,$result
}

function Get-Section($sections, [string]$name) {
    foreach ($s in $sections) { if ($s.Name -eq $name) { return $s } }
    return $null
}

function New-IniSection([string]$name) {
    [pscustomobject]@{
        Name   = $name
        Header = "[$name]"
        Lines  = (New-Object System.Collections.ArrayList)
    }
}

function Find-PluginSlot($sections, [string]$sectionName, [string]$dllName) {
    $sec = Get-Section $sections $sectionName
    if (-not $sec) { return -1 }
    foreach ($line in $sec.Lines) {
        if ($line -match '^\s*(\d+)\s*=\s*"?([^"]*)"?\s*$') {
            if ($matches[2] -like "*$dllName*") { return [int]$matches[1] }
        }
    }
    return -1
}

function Find-FreeSlot($sections) {
    $max = 0
    foreach ($s in $sections) {
        if ($s.Name -ne $ini_section -and $s.Name -ne $ini_section64) { continue }
        foreach ($line in $s.Lines) {
            if ($line -match '^\s*(\d+)\s*=') {
                $n = [int]$matches[1]
                if ($n -gt $max) { $max = $n }
            }
        }
    }
    if ($max -eq 0) { return 14 }
    return ($max + 1)
}

function Set-IniEntry($sections, [string]$sectionName, [int]$slot, [string]$value) {
    $target = Get-Section $sections $sectionName

    if (-not $target) {
        $target = New-IniSection $sectionName
        [void]$sections.Add($target)

        $hasContent = $false
        foreach ($s in $sections) {
            if ($s -ne $target -and ($s.Header -or $s.Lines.Count -gt 0)) { $hasContent = $true }
        }
        if ($hasContent) { [void]$target.Lines.Add('') }
    }

    $entry = "$slot=$value"
    $replaced = $false
    for ($i = 0; $i -lt $target.Lines.Count; $i++) {
        if ($target.Lines[$i] -match "^\s*$slot\s*=") {
            $target.Lines[$i] = $entry
            $replaced = $true
            break
        }
    }
    if (-not $replaced) {
        $trailing = 0
        while ($target.Lines.Count -gt 0 -and [string]::IsNullOrWhiteSpace($target.Lines[$target.Lines.Count - 1])) {
            $target.Lines.RemoveAt($target.Lines.Count - 1)
            $trailing++
        }
        [void]$target.Lines.Add($entry)
        for ($i = 0; $i -lt $trailing; $i++) { [void]$target.Lines.Add('') }
    }
    return $target
}

function Remove-IniEntry($sections, [string]$sectionName, [int]$slot) {
    $sec = Get-Section $sections $sectionName
    if (-not $sec) { return }
    for ($i = $sec.Lines.Count - 1; $i -ge 0; $i--) {
        if ($sec.Lines[$i] -match "^\s*$slot\s*=") { $sec.Lines.RemoveAt($i) }
    }
}

function ConvertTo-IniText($sections, [string]$eol = "`r`n") {
    $lines = @()
    foreach ($s in $sections) {
        if ($s.Header) { $lines += $s.Header }
        foreach ($l in $s.Lines) { $lines += $l }
    }
    return ($lines -join $eol)
}

# ---------------------------------------------------------------------------
#  Source files
# ---------------------------------------------------------------------------
function Get-SourceFile([string]$name) {
    foreach ($dir in @($script_dir, (Join-Path $script_dir 'dist'))) {
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        $candidate = Join-Path $dir $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    return $null
}

function Expand-FromZip([string]$zipPath, [string]$destDir) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $wanted = @($plugin_dll, $runtime_dll)
    $archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        foreach ($entry in $archive.Entries) {
            if ($wanted -notcontains $entry.Name) { continue }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $destDir $entry.Name), $true)
        }
    } finally { $archive.Dispose() }
}

# ---------------------------------------------------------------------------
#  Main
# ---------------------------------------------------------------------------
Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " $plugin_name WLX plugin installer" -ForegroundColor Cyan
Write-Host " works without administrator rights" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

# --- 1. Total Commander folder ---------------------------------------------
if ($TcDir) {
    if (-not (Test-TcFolder $TcDir)) { Write-Err "totalcmd.exe / totalcmd64.exe was not found in: $TcDir"; exit 1 }
    $tc_dir = (Resolve-Path -LiteralPath $TcDir).Path
    Write-Ok "Total Commander: $tc_dir"
} else {
    Write-Head "Looking for Total Commander..."
    $found = Find-TotalCommander
    if ($found) {
        $tc_dir = $found
        Write-Ok "Total Commander: $tc_dir"
    } else {
        Write-Warn2 "Total Commander was not found in the usual places."
        $picked = $null
        try {
            Add-Type -AssemblyName System.Windows.Forms
            $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
            $dlg.Description = 'Select the Total Commander folder (the one containing totalcmd.exe)'
            $dlg.ShowNewFolderButton = $false
            if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $picked = $dlg.SelectedPath }
        } catch { }
        if (-not $picked -and -not $Quiet) {
            $picked = Read-Host "Enter the full path to the Total Commander folder (empty to cancel)"
            if ($picked) { $picked = $picked.Trim('"') }
        }
        if (-not $picked -or -not (Test-TcFolder $picked)) {
            Write-Err "Total Commander folder was not selected. Nothing was changed."
            exit 1
        }
        $tc_dir = (Resolve-Path -LiteralPath $picked).Path
        Write-Ok "Total Commander: $tc_dir"
    }
}

$has64 = Test-Path -LiteralPath (Join-Path $tc_dir 'totalcmd64.exe') -PathType Leaf
$has32 = Test-Path -LiteralPath (Join-Path $tc_dir 'totalcmd.exe')   -PathType Leaf
if ($has32 -and -not $has64) {
    Write-Warn2 "This looks like a 32-bit Total Commander, but the plugin is a 64-bit DLL."
    Write-Note "It is installed for the 64-bit Total Commander; a 32-bit TC will not load it."
}

# --- 2. wincmd.ini ---------------------------------------------------------
if ($IniPath) {
    $ini_file = [pscustomobject]@{ Path = $IniPath; Source = 'parameter' }
} else {
    $ini_file = Resolve-IniFile $tc_dir
}
if ($ini_file.Source -ne 'missing') {
    Write-Ok "Configuration: $($ini_file.Path) ($($ini_file.Source))"
} else {
    Write-Warn2 "wincmd.ini was not found (checked the registry and the TC folder)."
}

# --- 3. Where to put the plugin files --------------------------------------
$classic_dir = Join-Path $tc_dir "plugins\wlx\$plugin_name"
$fallback_dir = $UserPluginDir
if (-not $fallback_dir) {
    if ($env:LOCALAPPDATA) { $fallback_dir = Join-Path $env:LOCALAPPDATA $plugin_name }
    else { $fallback_dir = Join-Path $env:APPDATA $plugin_name }
}

$canWriteTc = Test-CanInstallInto $tc_dir
if ($canWriteTc) { $plugin_dir = $classic_dir } else { $plugin_dir = $fallback_dir }

# --- 4. Uninstall ----------------------------------------------------------
if ($Uninstall) {
    Write-Head "Uninstalling..."
    Write-Note "TC folder     : $tc_dir"
    Write-Note "Configuration : $($ini_file.Path)"
    if (Test-Path -LiteralPath $ini_file.Path) {
        if (Test-FileWritable $ini_file.Path) {
            $ini = Read-IniFile $ini_file.Path
            $sections = Get-IniSections $ini.Text
            $slot = Find-PluginSlot $sections $ini_section $plugin_dll
            if ($slot -ge 0) {
                Copy-Item -LiteralPath $ini_file.Path -Destination "$($ini_file.Path).bak" -Force
                Remove-IniEntry $sections $ini_section $slot
                Remove-IniEntry $sections $ini_section64 $slot
                $ini.Text = ConvertTo-IniText $sections $ini.Eol
                Write-IniFile $ini $ini_file.Path
                Write-Ok "Entry $slot removed from wincmd.ini (backup: wincmd.ini.bak)"
            } else {
                Write-Warn2 "The plugin was not registered in wincmd.ini"
            }
        } else {
            Write-Warn2 "wincmd.ini is not writable; please remove the entry manually."
        }
    }
    foreach ($dir in @($classic_dir, $fallback_dir)) {
        if (Test-Path -LiteralPath $dir) {
            try {
                Remove-Item -LiteralPath $dir -Recurse -Force
                Write-Ok "Removed $dir"
            } catch {
                Write-Warn2 "Could not remove $dir (locked or no rights)"
            }
        }
    }
    Write-Host "`nDone. Restart Total Commander." -ForegroundColor Green
    if (-not $Quiet) { Read-Host "Press Enter to exit" | Out-Null }
    exit 0
}

# --- 5. Copy the files -----------------------------------------------------
Write-Head "Installing plugin files..."
try {
    New-Item -ItemType Directory -Path $plugin_dir -Force | Out-Null
} catch {
    Write-Err "Cannot create $plugin_dir : $($_.Exception.Message)"
    exit 1
}
if (-not (Test-DirWritable $plugin_dir)) {
    Write-Err "The folder exists but is not writable: $plugin_dir"
    exit 1
}
Write-Ok "Folder: $plugin_dir"

# --- Update: Total Commander keeps dwgviewer.wlx64 mapped, so it has to be
#     closed before the new DLL can replace the old one.
if ($Update) {
    $running = Get-Process -Name 'totalcmd64', 'totalcmd' -ErrorAction SilentlyContinue
    if ($running) {
        Write-Head "Closing Total Commander for the update..."
        foreach ($proc in $running) {
            try { $proc.CloseMainWindow() | Out-Null } catch { }
        }
        $deadline = (Get-Date).AddSeconds(20)
        while ((Get-Date) -lt $deadline -and
               (Get-Process -Name 'totalcmd64', 'totalcmd' -ErrorAction SilentlyContinue)) {
            Start-Sleep -Milliseconds 400
        }
        if (Get-Process -Name 'totalcmd64', 'totalcmd' -ErrorAction SilentlyContinue) {
            Write-Err "Total Commander is still running."
            Write-Note "Close it manually (it may be asking to save something) and run update again."
            if (-not $Quiet) { Read-Host "Press Enter to exit" | Out-Null }
            exit 1
        }
        Write-Ok "Total Commander closed"
    } else {
        Write-Ok "Total Commander is not running"
    }
}

$zip    = Get-SourceFile 'DwgViewer-win64.zip'
$srcDll = Get-SourceFile $plugin_dll
$srcRt  = Get-SourceFile $runtime_dll

if (-not $srcDll -and -not $zip) {
    Write-Err "$plugin_dll was not found next to install.ps1 (or in the dist subfolder)."
    Write-Err "Extract the whole release archive and run install.bat again."
    exit 1
}

try {
    if ($srcDll) {
        Copy-Item -LiteralPath $srcDll -Destination (Join-Path $plugin_dir $plugin_dll) -Force
        Write-Ok "Copied $plugin_dll"
    }
    if ($srcRt) {
        Copy-Item -LiteralPath $srcRt -Destination (Join-Path $plugin_dir $runtime_dll) -Force
        Write-Ok "Copied $runtime_dll"
    } elseif ($zip) {
        Expand-FromZip $zip $plugin_dir
        Write-Ok "Extracted $plugin_dll and $runtime_dll from DwgViewer-win64.zip"
    }
} catch {
    Write-Err "Could not copy the files: $($_.Exception.Message)"
    exit 1
}

# Keep dwgviewer.wlx64 in sync: a stale copy shadows the new plugin in TC x64.
$baseDll = Join-Path $plugin_dir $plugin_dll
$altDll  = Join-Path $plugin_dir $plugin_dll64
if (Test-Path -LiteralPath $baseDll) {
    try {
        Copy-Item -LiteralPath $baseDll -Destination $altDll -Force
        Write-Ok "Copied $plugin_dll64"
    } catch {
        Write-Err "Could not update $plugin_dll64 : $($_.Exception.Message)"
        Write-Note "Total Commander x64 loads '$plugin_dll64' when it exists."
        Write-Note "Close Total Commander completely and run install.bat again."
        exit 1
    }
}

$missing = @()
foreach ($f in @($plugin_dll, $runtime_dll)) {
    if (-not (Test-Path -LiteralPath (Join-Path $plugin_dir $f))) { $missing += $f }
}
if ($missing.Count -gt 0) {
    Write-Err "These files are missing after installation: $($missing -join ', ')"
    exit 1
}
Write-Ok "Both files are in place"

# --- 6. Register in wincmd.ini ---------------------------------------------
$dll_path = Join-Path $plugin_dir $plugin_dll

if ($NoIniUpdate) {
    Write-Warn2 "Skipped the wincmd.ini update (-NoIniUpdate)"
} elseif ($ini_file.Source -eq 'missing') {
    Write-Warn2 "There is no wincmd.ini to register the plugin in."
    Write-Note "Register it manually: Configuration > Options > Plugins > Lister > Configure > Add"
    Write-Note $dll_path
} else {
    $ini_path = $ini_file.Path

    if (-not (Test-FileWritable $ini_path)) {
        Write-Warn2 "wincmd.ini is read-only for the current user: $ini_path"

        $profileIni = Join-Path $env:APPDATA 'Ghisler\wincmd.ini'
        $canFix = $false

        if ($FixIni) {
            $canFix = ($profileIni -ne $ini_path)
        } elseif (-not $Quiet) {
            Write-Host ""
            Write-Host "  Total Commander stores its settings in a file this user cannot write." -ForegroundColor Yellow
            Write-Host "  The settings can be copied to the user profile:" -ForegroundColor Yellow
            Write-Host "    $profileIni" -ForegroundColor Yellow
            Write-Host "  Total Commander will use the copy (a value is set in HKCU, no admin rights)." -ForegroundColor Yellow
            $answer = Read-Host "  Move the configuration and register the plugin? [y/N]"
            $canFix = ($answer -match '^(y|yes)')
        }

        if ($canFix -and (Test-Path -LiteralPath $profileIni)) {
            Write-Warn2 "$profileIni already exists; the plugin is registered there without overwriting it."
            $ini_path = $profileIni
        } elseif ($canFix) {
            try {
                New-Item -ItemType Directory -Path (Split-Path -Parent $profileIni) -Force | Out-Null

                $copied = $false
                $srcIni = $null
                try {
                    $srcIni = Read-IniFile $ini_path
                    Write-IniFile $srcIni $profileIni
                    $copied = $true
                } catch {
                    Write-Warn2 "The existing wincmd.ini cannot be read; a new one will be created."
                }

                if ($copied) {
                    $copy = Read-IniFile $profileIni
                } else {
                    $copy = [pscustomobject]@{
                        Text     = "[Configuration]`r`n"
                        Encoding = [System.Text.Encoding]::Default
                        Preamble = [byte[]]@()
                        Eol      = "`r`n"
                    }
                }

                $secs = Get-IniSections $copy.Text
                $cfgSec = Get-Section $secs 'Configuration'
                if (-not $cfgSec) {
                    $cfgSec = New-IniSection 'Configuration'
                    [void]$secs.Insert(0, $cfgSec)
                }
                $hasKey = $false
                for ($i = 0; $i -lt $cfgSec.Lines.Count; $i++) {
                    if ($cfgSec.Lines[$i] -match '^\s*UseIniInProgramDir\s*=') {
                        $cfgSec.Lines[$i] = 'UseIniInProgramDir=0'
                        $hasKey = $true
                    }
                }
                if (-not $hasKey) {
                    $insertAt = 0
                    while ($insertAt -lt $cfgSec.Lines.Count -and [string]::IsNullOrWhiteSpace($cfgSec.Lines[$insertAt])) { $insertAt++ }
                    [void]$cfgSec.Lines.Insert($insertAt, 'UseIniInProgramDir=0')
                }
                $copy.Text = ConvertTo-IniText $secs $copy.Eol
                Write-IniFile $copy $profileIni

                if (-not (Test-Path $reg_key)) { New-Item -Path $reg_key -Force | Out-Null }
                Set-ItemProperty -Path $reg_key -Name 'IniFileName' -Value $profileIni

                Write-Ok "Configuration is now used from $profileIni (set in HKCU)"
                $ini_path = $profileIni
                $ini_file.Path = $profileIni
                $ini_file.Source = 'user profile'
            } catch {
                Write-Err "Could not move wincmd.ini: $($_.Exception.Message)"
                $ini_path = $null
            }
        } else {
            Write-Note "Left unchanged. Register the plugin manually:"
            Write-Note "Configuration > Options > Plugins > Lister > Configure > Add -> $dll_path"
            $ini_path = $null
        }
    }

    if ($ini_path) {
        Write-Head "Registering the plugin..."
        $ini = Read-IniFile $ini_path
        $sections = Get-IniSections $ini.Text

        $slot = Find-PluginSlot $sections $ini_section $plugin_dll
        $existed = $slot -ge 0
        if (-not $existed) { $slot = Find-FreeSlot $sections }

        Copy-Item -LiteralPath $ini_path -Destination "$ini_path.bak" -Force
        Write-Ok "Backup: wincmd.ini.bak"

        [void](Set-IniEntry $sections $ini_section $slot "`"$dll_path`"")
        [void](Set-IniEntry $sections $ini_section64 $slot '1')

        $ini.Text = ConvertTo-IniText $sections $ini.Eol
        Write-IniFile $ini $ini_path

        $check = Read-IniFile $ini_path
        $checkSections = Get-IniSections $check.Text
        $secPath = Get-Section $checkSections $ini_section
        $sec64   = Get-Section $checkSections $ini_section64
        $okPath = $secPath -and (($secPath.Lines -join "`n") -match [regex]::Escape($dll_path))
        $ok64   = $sec64   -and (($sec64.Lines   -join "`n") -match "(?m)^\s*$slot\s*=\s*1\s*$")

        if ($okPath -and $ok64) {
            $verb = if ($existed) { 'updated' } else { 'added' }
            Write-Ok "Entry $slot $verb in [$ini_section] and [$ini_section64]"
        } else {
            Write-Warn2 "The new entries could not be verified; please check wincmd.ini."
        }
    }
}

# --- 7. Summary ------------------------------------------------------------
$exe = if ($has64) { 'totalcmd64.exe' } else { 'totalcmd.exe' }
$ver = ''
try { $ver = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $tc_dir $exe)).FileVersion } catch { }

Write-Host "`n==============================================" -ForegroundColor Green
Write-Host " Installation complete" -ForegroundColor Green
Write-Host "==============================================" -ForegroundColor Green
Write-Host "Plugin folder : $plugin_dir"
Write-Host "Configuration : $($ini_file.Path)"
if ($ver) { Write-Host "TC version    : $ver" }
Write-Host ""
Write-Host "Close and start Total Commander again, then press F3 on any .dwg file." -ForegroundColor Yellow

if (-not $Quiet) { Read-Host "Press Enter to exit" | Out-Null }
exit 0
