# cleanup.ps1 - уборка репозитория GitHub (шаг 6 из PUBLISH.md):
#   1) помечает указанные старые релизы как предварительные (--prerelease)
#   2) удаляет упавшие прогоны GitHub Actions
#
# Запуск:
#   powershell -ExecutionPolicy Bypass -File build\cleanup.ps1
#   powershell -ExecutionPolicy Bypass -File build\cleanup.ps1 -DryRun
#
# Требуется GitHub CLI (gh) 2.x, авторизованный:  gh auth login

[CmdletBinding()]
param(
    [string]   $Repo = 'cherneyivan-cloud/dwgviewer-wlx',
    # Релизы, помечаемые как предварительные (v1.0.0 — архив до фиксов;
    # при желании добавьте 'v1.1.0'):
    [string[]] $Prereleases = @('v1.0.0'),
    [switch]   $DryRun
)

$ErrorActionPreference = 'Stop'

function Invoke-Gh {
    param([string[]]$GhArgs)
    if ($DryRun) {
        Write-Host ('  [dry-run] gh ' + ($GhArgs -join ' '))
        return
    }
    & gh @GhArgs
    if ($LASTEXITCODE -ne 0) {
        throw ('gh ' + ($GhArgs -join ' ') + ' завершился с кодом ' + $LASTEXITCODE)
    }
}

# --- проверки окружения -----------------------------------------------------
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Error "Не найден GitHub CLI (gh). Установите его и выполните 'gh auth login'."
}
gh auth status *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Error "gh не авторизован. Выполните 'gh auth login'."
}

# --- 1. Старые релизы -> prerelease -----------------------------------------
Write-Host '== Шаг 1. Релизы -> prerelease =='
foreach ($tag in $Prereleases) {
    gh release view $tag --repo $Repo *> $null
    if ($LASTEXITCODE -eq 0) {
        Invoke-Gh -GhArgs @('release', 'edit', $tag, '--repo', $Repo, '--prerelease')
        Write-Host ('  ' + $tag + ' -> prerelease')
    } else {
        Write-Host ('  ' + $tag + ': релиз не найден, пропуск')
    }
}

# --- 2. Удаление упавших прогонов CI ----------------------------------------
Write-Host '== Шаг 2. Удаление упавших прогонов =='
if ($DryRun) {
    Write-Host '  [dry-run] gh run list --status failure'
    Write-Host '  [dry-run] gh run delete <ID>  (для каждого упавшего прогона)'
} else {
    $ids = @(gh run list --repo $Repo --status failure --limit 100 --json databaseId --jq '.[].databaseId')
    if ($ids.Count -eq 0) {
        Write-Host '  упавших прогонов нет'
    } else {
        foreach ($id in $ids) {
            if ([string]::IsNullOrWhiteSpace($id)) { continue }
            Invoke-Gh -GhArgs @('run', 'delete', $id, '--repo', $Repo)
            Write-Host ('  удалён упавший run ' + $id)
        }
    }
}

if ($DryRun) {
    Write-Host 'Готово.  (dry-run — изменения не вносились)'
} else {
    Write-Host 'Готово.'
}