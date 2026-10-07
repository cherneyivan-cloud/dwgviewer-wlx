# Публикация плагина на GitHub — пошаговая инструкция

## 0. Что уже готово в репозитории

- исходники плагина: `src/` (dwgwlx, model, draw, aci, dwgload);
- сборочные скрипты: `build.bat`, `build\setup_toolchain.ps1`,
  `build\install.ps1` (всё использует относительные пути);
- документация: `README.md`, `LICENSE` (GPLv3),
  `PUBLISH.md` (этот файл);
- сборка на GitHub Actions: `.github\workflows\build.yml`.

В Git **не попадают** (см. `.gitignore`): качаемые тулчейн/библиотеки
(`tools/`, `dl/`, `third_party/libredwg*`), результат сборки (`dist/`),
логи и тестовые артефакты. Пользователь получает готовый двоичный
архив из раздела **Releases**.

---

## 1. Установите Git (если нет)

- https://git-scm.com/download/win — «Next» до конца.
- Проверка: `git --version`

## 2. Инициализируйте репозиторий и сделайте первый коммит

Откройте командную строку в папке проекта (`C:\tmp\tc wlx`):

```bat
git init
git add .
git commit -m "DWG Viewer WLX plugin for Total Commander x64 - v1.0"
```

> Убедитесь, что `git status` не показывает `dist/`, `tools/`,
> `dl/`, `third_party/libredwg/` — они игнорируются.

## 3. Создайте репозиторий на GitHub

1. Зайдите на https://github.com и **New repository**:
   - имя, например `dwgviewer-wlx` (или любое);
   - Public (рекомендую) или Private;
   - **не** создавайте файлы (README/.gitignore/LICENSE — они уже есть).
2. На странице репозитория скопируйте URL:
   `https://github.com/<ВАШ_ЛОГИН>/<репозиторий>.git`

## 4. Подключите удалённый репозиторий и отправьте код

```bat
git remote add origin https://github.com/<ВАШ_ЛОГИН>/<репозиторий>.git
git branch -M main
git push -u origin main
```

## 5. Создайте Release с готовым плагином

Локально: `git tag v1.0.0 && git push origin v1.0.0`

Затем на GitHub: **Releases → Create a new release** → выбрать тег `v1.0.0`
→ приложить архивы:

```bat
cd dist
tar -a -c -f DwgViewer-win64.zip dwgviewer.wlx libredwg-0.dll README.md LICENSE   REM бинарники
tar -a -c -f DwgViewer-src.zip  src build devtest .github .gitignore build.bat README.md LICENSE PUBLISH.md   REM исходники
```

Приложите **оба** архива (`DwgViewer-win64.zip` + `DwgViewer-src.zip`) → Publish release.
Кроме того, GitHub автоматически добавляет к релизу архив исходников
(release tag `Source code (zip/tar.gz)`) — дублировать не обязательно.

> **Вместо ручного шага** теги v* автоматически собираются и прикладывают
> оба архива через `.github/workflows/build.yml` (см. шаг 7).

## 6. (По желанию) Сборка на GitHub Actions — автоматом

Когда вы запушете тег `v*`, workflow:
- скачает w64devkit и предсобранный LibreDWG 0.14 (win64);
- соберёт `dwgviewer.wlx`;
- упакует `dwgviewer-win64.zip` и прикрепит к Release.

Выберите: **Actions → build → Run workflow** (или просто запушьте тег).

## 7. (По желанию) Проверка на чистой машине

Клонируйте репозиторий и:

```bat
build.bat                      REM соберёт dist\dwgviewer.wlx
build\install.ps1 "C:\Program Files\totalcmd"
```

`install.ps1` скопирует файлы в `plugins\wlx\dwgviewer\` и пропишет
плагин в `wincmd.ini` (см. установку/обновление в `dist\README.md`).

---

## Чего не делалось и почему

- **Не кладём** `libredwg-0.dll` и тулчейн в Git: они большие и
  генерируются/качаются автоматически. Библиотека LibreDWG — GNU GPLv3,
  распространяется вместе с плагином как отдельный файл (см. `LICENSE`).
- Для 32-битной версии Total Commander нужна 32-битная сборка: x86-тулчейн
  w64devkit и `libredwg-0.14-win32.zip` (можно добавить вторым job`ом CI).

## Полезные ссылки

- LibreDWG: https://github.com/LibreDWG/libredwg , https://www.gnu.org/software/libredwg/
- API WLX-плагинов Lister: https://www.ghisler.com/wlx_tc.htm (и официальный SDK в поставке TC)
- Total Commander plugins: https://www.totalcmd.net/ , https://totalcmd.net/menumg.htm