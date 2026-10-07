# Скачивание и настройка тулчейна (w64devkit) и либрарии LibreDWG.
# Идемпотентно: если всё уже на месте, ничего не делает.
$ErrorActionPreference = 'Stop'
# Каталог проекта = родитель папки build/
$root = Split-Path $PSScriptRoot -Parent
$tools = Join-Path $root 'tools'
$td    = Join-Path $root 'third_party'
$dl    = Join-Path $root 'dl'
New-Item -ItemType Directory -Force -Path $tools, $td, $dl | Out-Null

# --- w64devkit x64 (портативный GCC/MinGW-w64) ---
$wdkDir = Join-Path $tools 'w64devkit'
if (-not (Test-Path (Join-Path $wdkDir 'bin\gcc.exe'))) {
    $wdkSfx = Join-Path $dl 'w64devkit-x64-2.10.0.7z.exe'
    if (-not (Test-Path $wdkSfx)) {
        Write-Host 'Downloading w64devkit (x64)...'
        Invoke-WebRequest -Uri 'https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x64-2.10.0.7z.exe' -OutFile $wdkSfx
    }
    $7zr = Join-Path $dl '7zr.exe'
    if (-not (Test-Path $7zr)) {
        Write-Host 'Downloading 7zr.exe...'
        Invoke-WebRequest -Uri 'https://www.7-zip.org/a/7zr.exe' -OutFile $7zr
    }
    Write-Host 'Extracting w64devkit...'
    & $7zr x $wdkSfx -o"$tools\_wdk" -y | Out-Null
    if (Test-Path "$tools\_wdk\w64devkit\bin\gcc.exe") {
        Move-Item "$tools\_wdk\w64devkit" $wdkDir
        Remove-Item "$tools\_wdk" -Recurse -Force
    } elseif (Test-Path "$tools\_wdk\bin\gcc.exe") {
        Move-Item "$tools\_wdk" $wdkDir
    } else {
        throw 'w64devkit: gcc not found after extraction'
    }
    if (-not (Test-Path (Join-Path $wdkDir 'bin\gcc.exe'))) {
        throw 'w64devkit: gcc.exe missing'
    }
}
Write-Host "w64devkit ready: $wdkDir"

# --- LibreDWG: предсобранная бинарная библиотека (win64) ---
$lrPre = Join-Path $td 'libredwg'
if (-not (Test-Path (Join-Path $lrPre 'include\dwg.h'))) {
    $zipDir = Join-Path $td '_libredwg_zip'
    if (-not (Test-Path (Join-Path $zipDir 'include\dwg.h'))) {
        $zip = Join-Path $dl 'libredwg-0.14-win64.zip'
        if (-not (Test-Path $zip)) {
            Write-Host 'Downloading LibreDWG prebuilt (win64)...'
            Invoke-WebRequest -Uri 'https://github.com/LibreDWG/libredwg/releases/download/0.14/libredwg-0.14-win64.zip' -OutFile $zip
        }
        New-Item -ItemType Directory -Force -Path $zipDir | Out-Null
        tar -xf $zip -C $zipDir
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $lrPre 'include'),
                                               (Join-Path $lrPre 'lib'),
                                               (Join-Path $lrPre 'bin') | Out-Null
    Copy-Item -Recurse -Force (Join-Path $zipDir 'include\*') (Join-Path $lrPre 'include')
    Copy-Item -Recurse -Force (Join-Path $zipDir 'lib\*')     (Join-Path $lrPre 'lib')
    Copy-Item -Force (Join-Path $zipDir 'libredwg-0.dll')     (Join-Path $lrPre 'bin')
}
Write-Host "LibreDWG ready: $lrPre"

# --- LibreDWG исходники (сборка из исходников не нужна, но оставим для справки) ---
$lrSrc = Join-Path $td 'libredwg-src'
if (-not (Test-Path (Join-Path $lrSrc 'meson.build'))) {
    $tarxz = Join-Path $dl 'libredwg-0.14.tar.xz'
    if (-not (Test-Path $tarxz)) {
        Write-Host 'Downloading LibreDWG sources (optional)...'
        Invoke-WebRequest -Uri 'https://github.com/LibreDWG/libredwg/releases/download/0.14/libredwg-0.14.tar.xz' -OutFile $tarxz
    }
    Write-Host 'Extracting LibreDWG sources...'
    python -c "import tarfile; tarfile.open(r'$tarxz','r:xz').extractall(r'$td')"
    if (Test-Path (Join-Path $td 'libredwg-0.14')) {
        Move-Item (Join-Path $td 'libredwg-0.14') $lrSrc
    }
}
Write-Host "DONE"