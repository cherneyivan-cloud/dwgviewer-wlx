#!/usr/bin/env bash
# cleanup.sh - уборка репозитория GitHub (шаг 6 из PUBLISH.md):
#   1) помечает указанные старые релизы как предварительные (--prerelease)
#   2) удаляет упавшие прогоны GitHub Actions
#
# Запуск (из каталога репозитория):
#   bash build/cleanup.sh            # выполнить
#   bash build/cleanup.sh --dry-run  # только показать действия
#
# Требуется GitHub CLI (gh) 2.x, авторизованный:  gh auth login
set -euo pipefail

REPO="cherneyivan-cloud/dwgviewer-wlx"

# Релизы, помечаемые как предварительные:
#   v1.0.0 — архив до фиксов; при желании добавьте второй элемент "v1.1.0"
PRERELEASES=("v1.0.0")

DRY_RUN=0
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
fi

log() { printf '%s\n' "$*"; }
run() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    log "  [dry-run] $*"
  else
    "$@"
  fi
}

# --- проверки окружения -----------------------------------------------------
if ! command -v gh >/dev/null 2>&1; then
  echo "ОШИБКА: не найден GitHub CLI (gh). Установите и выполните 'gh auth login'." >&2
  exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
  echo "ОШИБКА: gh не авторизован. Выполните 'gh auth login'." >&2
  exit 1
fi

# --- 1. Старые релизы -> prerelease -----------------------------------------
log "== Шаг 1. Релизы -> prerelease =="
for tag in "${PRERELEASES[@]}"; do
  if gh release view "$tag" --repo "$REPO" >/dev/null 2>&1; then
    run gh release edit "$tag" --repo "$REPO" --prerelease
    log "  ${tag} -> prerelease"
  else
    log "  ${tag}: релиз не найден, пропуск"
  fi
done

# --- 2. Удаление упавших прогонов CI ----------------------------------------
log "== Шаг 2. Удаление упавших прогонов =="
FAILED=()
while IFS= read -r id; do
  [[ -n "$id" ]] && FAILED+=("$id")
done < <(gh run list --repo "$REPO" --status failure --limit 100 \
                 --json databaseId --jq '.[].databaseId')

if [[ "${#FAILED[@]}" -eq 0 ]]; then
  log "  упавших прогонов нет"
else
  for id in "${FAILED[@]}"; do
    run gh run delete "$id" --repo "$REPO"
    log "  удалён упавший run ${id}"
  done
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "Готово.  (dry-run — изменения не вносились)"
else
  log "Готово."
fi