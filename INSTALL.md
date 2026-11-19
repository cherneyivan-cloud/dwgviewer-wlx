# DWG Viewer WLX Plugin для Total Commander

Быстрый просмотр DWG-чертежей прямо в Total Commander без необходимости в CAD.

## Особенности

- ✓ Поддержка DWG R2000–R2023
- ✓ Отображение блоков, размеров, выносок, штриховок
- ✓ Коррректное отображение Cyrillic текста
- ✓ Поворот и перенос текста по заданным параметрам
- ✓ Автоматический зум и панорамирование
- ✓ Горячие клавиши (Home/F/Num5 — восстановить масштаб, V/H/B — горизонтальный/вертикальный/180° поворот)

## Системные требования

- **ОС:** Windows 7+ (x64 рекомендуется)
- **Total Commander:** v9.0+ (32-bit или 64-bit)
- **Права:** НЕ требуются права администратора

## Установка

### Способ 1: Автоматическая установка (рекомендуется)

1. Распакуйте архив в удобное место
2. **Запустите `install.bat`** (или `powershell -ExecutionPolicy Bypass -File install.ps1`)
3. Следуйте инструкциям на экране:
   - Скрипт найдёт Total Commander автоматически
   - Если TC не найден, вы сможете выбрать папку вручную
   - Проверит наличие всех файлов и обновит `wincmd.ini`
4. **Закройте и заново откройте Total Commander**
5. Готово! Теперь `F3` на DWG-файле покажет его с плагином

### Способ 2: Ручная установка

Если автоматическая установка не сработала:

1. Скопируйте файлы в папку TC:
   ```
   C:\Program Files\totalcmd\plugins\wlx\DwgViewer\
     - dwgviewer.wlx
     - dwgviewer.wlx64 (для 64-bit TC)
     - libredwg-0.dll
   ```

2. Отредактируйте `C:\Program Files\totalcmd\wincmd.ini`:
   ```ini
   [ListerPlugins]
   14="C:\Program Files\totalcmd\plugins\wlx\DwgViewer\dwgviewer.wlx"
   
   [ListerPlugins64]
   14=1
   ```

3. Перезагрузите Total Commander

## Использование

| Действие | Клавиша |
|----------|---------|
| Восстановить масштаб | Home, F, или Num5 |
| Зум | Колесо мыши |
| Панорамирование | Перетаскивание мышью |
| Повернуть горизонтально | V |
| Повернуть вертикально | H |
| Повернуть на 180° | B |

## Поддержка

- Обнаруженные ошибки: https://github.com/cherneyivan-cloud/dwgviewer-wlx/issues
- Последняя версия: https://github.com/cherneyivan-cloud/dwgviewer-wlx/releases

## Лицензия

GNU General Public License v3.0 — смотрите файл `LICENSE`

---

**Версия:** 1.2.5  
**Обновлено:** 2026-10-08  
**Автор:** cherneyivan-cloud
