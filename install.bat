# install.ps1 - DWG Viewer WLX plugin installer for Total Commander
# Run: powershell -ExecutionPolicy Bypass -File install.ps1
# No admin rights required (installs to TC plugins folder)

$ErrorActionPreference = 'Continue'
$script_dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$plugin_name = 'DwgViewer'
$plugin_dll = 'dwgviewer.wlx'
$plugin_dll64 = 'dwgviewer.wlx64'
$runtime_dll = 'libredwg-0.dll'

Write-Host "=== $plugin_name WLX Plugin Installer ===" -ForegroundColor Cyan
Write-Host "No admin rights required. Installing to Total Commander plugins folder.`n"

# --- Find Total Commander installation ---
Write-Host "Searching for Total Commander installation..." -ForegroundColor Yellow

$tc_paths = @(
    "C:\Program Files\totalcmd",
    "C:\Program Files (x86)\totalcmd",
    "C:\totalcmd",
    "$env:LOCALAPPDATA\totalcmd"
)

$tc_dir = $null
foreach ($path in $tc_paths) {
    if (Test-Path "$path\totalcmd.exe" -o Test-Path "$path\totalcmd64.exe") {
        $tc_dir = $path
        Write-Host "Found: $tc_dir" -ForegroundColor Green
        break
    }
}

if (-not $tc_dir) {
    Write-Host "Total Commander not found in standard locations." -ForegroundColor Yellow
    Write-Host "Please select the Total Commander installation folder manually."
    
    Add-Type -AssemblyName System.Windows.Forms
    $browser = New-Object System.Windows.Forms.FolderBrowserDialog
    $browser.Description = "Select Total Commander installation folder"
    $browser.RootFolder = [System.Environment+SpecialFolder]::MyComputer
    
    if ($browser.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        $tc_dir = $browser.SelectedPath
        if (-not (Test-Path "$tc_dir\totalcmd.exe" -o Test-Path "$tc_dir\totalcmd64.exe")) {
            Write-Host "ERROR: totalcmd.exe or totalcmd64.exe not found in $tc_dir" -ForegroundColor Red
            Read-Host "Press Enter to exit"
            exit 1
        }
        Write-Host "Selected: $tc_dir" -ForegroundColor Green
    } else {
        Write-Host "Installation cancelled." -ForegroundColor Red
        exit 1
    }
}

# --- Verify TC directory ---
$wincmd_ini = "$tc_dir\wincmd.ini"
if (-not (Test-Path $wincmd_ini)) {
    Write-Host "WARNING: wincmd.ini not found at $wincmd_ini" -ForegroundColor Yellow
    Write-Host "TC may not be correctly installed." -ForegroundColor Yellow
}

$tc_version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo("$tc_dir\totalcmd.exe").FileVersion
Write-Host "Total Commander version: $tc_version" -ForegroundColor Cyan

# --- Prepare plugin directory ---
$plugin_dir = "$tc_dir\plugins\wlx\$plugin_name"
Write-Host "Plugin directory: $plugin_dir" -ForegroundColor Cyan

if (-not (Test-Path $plugin_dir)) {
    Write-Host "Creating plugin directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $plugin_dir -Force | Out-Null
}

# --- Find and extract archive or copy files ---
# Search in current directory and dist subdirectory
$search_dirs = @($script_dir, "$script_dir\dist")
$zip_file = $null
$src_dll = $null
$src_dll64 = $null
$src_runtime = $null

foreach ($dir in $search_dirs) {
    if (-not (Test-Path $dir)) { continue }
    if (-not $zip_file) {
        $zip_file = Get-ChildItem -Path $dir -Filter "DwgViewer-win64.zip" -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (-not $src_dll) {
        $src_dll = Get-ChildItem -Path $dir -Filter $plugin_dll -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (-not $src_dll64) {
        $src_dll64 = Get-ChildItem -Path $dir -Filter $plugin_dll64 -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (-not $src_runtime) {
        $src_runtime = Get-ChildItem -Path $dir -Filter $runtime_dll -ErrorAction SilentlyContinue | Select-Object -First 1
    }
}

if ($zip_file) {
    Write-Host "Extracting from: $($zip_file.FullName)" -ForegroundColor Yellow
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip_file.FullName, $plugin_dir, $true)
        Write-Host "Extracted successfully." -ForegroundColor Green
    } catch {
        Write-Host "ERROR: Failed to extract: $_" -ForegroundColor Red
        Read-Host "Press Enter to exit"
        exit 1
    }
} elseif ($src_dll -and $src_runtime) {
    Write-Host "Copying files from: $script_dir" -ForegroundColor Yellow
    try {
        Copy-Item $src_dll.FullName "$plugin_dir\$plugin_dll" -Force
        Copy-Item $src_dll.FullName "$plugin_dir\$plugin_dll64" -Force
        Copy-Item $src_runtime.FullName "$plugin_dir\$runtime_dll" -Force
        if ($src_dll64) {
            Copy-Item $src_dll64.FullName "$plugin_dir\$plugin_dll64" -Force
        }
        Write-Host "Files copied successfully." -ForegroundColor Green
    } catch {
        Write-Host "ERROR: Failed to copy files: $_" -ForegroundColor Red
        Read-Host "Press Enter to exit"
        exit 1
    }
} else {
    Write-Host "ERROR: Neither DwgViewer-win64.zip nor plugin files found in $script_dir" -ForegroundColor Red
    Write-Host "Expected: DwgViewer-win64.zip or $plugin_dll + $runtime_dll" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# --- Verify installed files ---
Write-Host "Verifying installed files..." -ForegroundColor Yellow
$installed_files = @($plugin_dll, $plugin_dll64, $runtime_dll)
$missing = @()
foreach ($file in $installed_files) {
    if (-not (Test-Path "$plugin_dir\$file")) {
        $missing += $file
    }
}

if ($missing.Count -gt 0) {
    Write-Host "WARNING: Missing files after installation: $($missing -join ', ')" -ForegroundColor Yellow
} else {
    Write-Host "All plugin files verified OK." -ForegroundColor Green
}

# --- Update wincmd.ini ---
if (Test-Path $wincmd_ini) {
    Write-Host "Updating wincmd.ini..." -ForegroundColor Yellow
    
    $content = Get-Content $wincmd_ini -Raw -ErrorAction SilentlyContinue
    if (-not $content) {
        Write-Host "WARNING: Could not read wincmd.ini" -ForegroundColor Yellow
    } else {
        $backup_ini = "$wincmd_ini.bak"
        Copy-Item $wincmd_ini $backup_ini -Force
        Write-Host "Backup created: $backup_ini" -ForegroundColor Cyan
        
        # Parse INI sections and update [ListerPlugins] and [ListerPlugins64]
        $lines = $content -split "`n"
        $new_lines = @()
        $in_lister = $false
        $in_lister64 = $false
        $plugin_entry = "14=`"$plugin_dir\$plugin_dll`""
        $plugin_entry64 = "14=1"
        $found_14 = $false
        $found_14_64 = $false
        
        foreach ($line in $lines) {
            $trimmed = $line.Trim()
            
            if ($trimmed -eq "[ListerPlugins]") {
                $in_lister = $true
                $in_lister64 = $false
                $new_lines += $line
            } elseif ($trimmed -eq "[ListerPlugins64]") {
                $in_lister = $false
                $in_lister64 = $true
                $new_lines += $line
            } elseif ($trimmed -match "^\[" -and $trimmed -ne "[ListerPlugins]" -and $trimmed -ne "[ListerPlugins64]") {
                $in_lister = $false
                $in_lister64 = $false
                
                # Before switching sections, add missing plugin entries if not found
                if (-not $found_14 -and $in_lister) {
                    $new_lines += $plugin_entry
                }
                if (-not $found_14_64 -and $in_lister64) {
                    $new_lines += $plugin_entry64
                }
                
                $new_lines += $line
            } elseif ($in_lister -and $trimmed -match "^14=") {
                $found_14 = $true
                $new_lines += $plugin_entry
            } elseif ($in_lister64 -and $trimmed -match "^14=") {
                $found_14_64 = $true
                $new_lines += $plugin_entry64
            } else {
                $new_lines += $line
            }
        }
        
        # Ensure both sections exist and have entry 14
        if (-not ($content -match "\[ListerPlugins\]")) {
            $new_lines += "`n[ListerPlugins]"
            $new_lines += $plugin_entry
        } elseif (-not $found_14) {
            $idx = $new_lines.IndexOf($new_lines | Where-Object { $_ -match "^\[ListerPlugins\]" })
            if ($idx -ge 0) {
                $new_lines.Insert($idx + 1, $plugin_entry)
            }
        }
        
        if (-not ($content -match "\[ListerPlugins64\]")) {
            $new_lines += "`n[ListerPlugins64]"
            $new_lines += $plugin_entry64
        } elseif (-not $found_14_64) {
            $idx = $new_lines.IndexOf($new_lines | Where-Object { $_ -match "^\[ListerPlugins64\]" })
            if ($idx -ge 0) {
                $new_lines.Insert($idx + 1, $plugin_entry64)
            }
        }
        
        $new_content = $new_lines -join "`n"
        Set-Content $wincmd_ini $new_content -Encoding UTF8 -Force
        Write-Host "wincmd.ini updated successfully." -ForegroundColor Green
    }
} else {
    Write-Host "WARNING: wincmd.ini not found. Manual registration may be needed:" -ForegroundColor Yellow
    Write-Host "  [ListerPlugins]" -ForegroundColor Gray
    Write-Host "  14=`"$plugin_dir\$plugin_dll`"" -ForegroundColor Gray
    Write-Host "  [ListerPlugins64]" -ForegroundColor Gray
    Write-Host "  14=1" -ForegroundColor Gray
}

# --- Complete ---
Write-Host "`n=== Installation Complete ===" -ForegroundColor Green
Write-Host "Plugin: $plugin_name" -ForegroundColor Cyan
Write-Host "Location: $plugin_dir" -ForegroundColor Cyan
Write-Host "TC Location: $tc_dir" -ForegroundColor Cyan
Write-Host "`nIMPORTANT: Close and reopen Total Commander to load the plugin." -ForegroundColor Yellow
Write-Host "Then press F3 on any .DWG file to view it with $plugin_name." -ForegroundColor Yellow

Read-Host "`nPress Enter to exit"
