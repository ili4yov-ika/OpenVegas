# Инструменты проекта OpenVegas

Скрипты сборки пакетов, WSL/macOS-окружение и утилиты разработки.

## macOS

```bash
chmod +x tools/setup_macos.sh tools/macos_build.sh
bash tools/setup_macos.sh          # Homebrew: cmake, ninja, qt@6 → ~/.openvegas_macos_env.sh
source ~/.openvegas_macos_env.sh

bash tools/macos_build.sh                 # Debug (preset macos-debug)
bash tools/macos_build.sh release         # Release
bash tools/macos_build.sh release deploy  # + macdeployqt → build/macos/OpenVegas.app
```

Требуется: macOS 11+, Xcode CLT, Homebrew. CMake Presets: `macos-debug`, `macos-release` (`CMakePresets.json`).
Qt ищется через `CMAKE_PREFIX_PATH` (Homebrew `qt@6` или `~/Qt/6.x/macos`).

## WSL (Debian/Ubuntu)

```bash
bash tools/setup_wsl_debian.sh     # пакеты + aqtinstall Qt 6.8 + сборка
source ~/.openvegas_wsl_env.sh
./build/linux-wsl/OpenVegas
```

Шаблон env без установки: `tools/wsl_debian_env.sh`.

## FFmpeg (Windows portable)

Если `ffmpeg` нет в PATH, OpenVegas ищет его рядом с exe:

- `OpenVegas.exe` + `ffmpeg.exe` (та же папка)
- `ffmpeg/bin/ffmpeg.exe`, `bin/ffmpeg.exe`, `tools/ffmpeg/bin/ffmpeg.exe`

То же для `ffprobe.exe`. Удобно положить Gyan/BtbN full build в `ffmpeg/bin/` в каталоге сборки или инсталлятора.

```text
build/Windows_MinGW-x64/
  OpenVegas.exe
  ffmpeg.exe          # или
  ffmpeg/bin/ffmpeg.exe
  ffmpeg/bin/ffprobe.exe
```

## Структура (установщики)

- `build_windows_installer.bat` — Release-сборка + `windeployqt` + NSIS
- `build_deb.sh` — Debian/Ubuntu пакет (`.deb`)
- `build_rpm.sh` — Fedora/RHEL пакет (`.rpm`)
- `nsis_installer.nsi` — скрипт NSIS (приложение + ассоциация `.veg`)
- `openvegas.desktop` — ярлык Linux
- `debian/` — метаданные для `dpkg-buildpackage`
- `rpm/openvegas.spec` — spec для `rpmbuild`
- `svg_to_ico.py` / `make_app_icon.py` — `logo.svg` → `logo.ico` + PNG (Pillow)

## Использование

### Windows

```batch
tools\build_windows_installer.bat
```

Требуется:

- CMake
- Qt 6.8+ (Widgets + Svg; MSVC или MinGW)
- NSIS ([скачать](https://nsis.sourceforge.io/Download))

Переменные окружения (обе необязательны):

| Переменная | Зачем |
|------------|-------|
| `CMAKE_PREFIX_PATH` | корень Qt-кита, например `C:\Qt\6.9.3\mingw_64` |
| `OPENVEGAS_CMAKE_GENERATOR` | генератор CMake, например `Ninja` или `MinGW Makefiles`; не задан — CMake выбирает сам |

Скрипт собирает в отдельное дерево `build\windows-installer` (Release), а не в
`build\Windows_MinGW-x64` из `CMakePresets.json`: тот сконфигурирован под Debug,
и переиспользование заставляло бы полностью пересобирать проект при каждом
переключении между ними.

`windeployqt` берётся из того кита, которым реально собрано, — путь читается из
`Qt6_DIR` в `CMakeCache.txt`. Это важно, когда рядом стоит несколько китов
(`mingw_64`, `llvm-mingw_64`, `msvc2022_64`): развёрнутые DLL от «не того» кита
дают инсталлятор, падающий на старте.

Результат: `tools\OpenVegas_Setup.exe`.

### Linux (Debian/Ubuntu)

```bash
chmod +x tools/build_deb.sh
tools/build_deb.sh
```

Требуется: CMake, Qt6 (`qt6-base-dev`, `qt6-svg-dev`), `dpkg-buildpackage` / `devscripts`, `debhelper`.

Сборку выполняет сам `dpkg-buildpackage` через `debian/rules`; скрипт только
копирует `tools/debian/` в корень проекта (иначе `dpkg-buildpackage` его не найдёт)
и удаляет копию на выходе, в том числе при ошибке. Если `debian/` в корне уже есть,
скрипт останавливается, а не перезаписывает его.

### Linux (Fedora/RHEL)

```bash
chmod +x tools/build_rpm.sh
tools/build_rpm.sh
```

Требуется: CMake, Qt6, `rpmbuild`, `rpmdevtools`.

## Альтернатива: CPack

После `cmake --build` / `cmake --install`:

```bash
# Windows
cpack -G NSIS

# Linux
cpack -G DEB   # или -G RPM

# macOS (после Release в build/macos)
cpack -G DragNDrop
```

Настройки CPack — в корневом `CMakeLists.txt`.

## Переводы, окончания строк

### Утилиты переводов (опционально)

Скрипты `finalize_translations.py`, `fix_translations.py`, `apply_remaining_en.py`,
`clean_ts_vanished.py` — помощники для Qt `.ts`/`.qm`, когда в проекте появятся
файлы локализации. Сейчас их нет: `CMakeLists.txt` не подключает `LinguistTools`
и ничего не ставит в `share/*/translations`.

### Окончания строк

`.gitattributes` в корне жёстко фиксирует: `.bat`/`.cmd`/`.ps1` — CRLF, `.sh` и
`tools/debian/*` — LF. Это не косметика: `cmd.exe` не разбирает `.bat` с LF (ломает
каждую строку и валится с бессмысленными ошибками), а shell-скрипт с CRLF падает
на `bad interpreter: /bin/bash^M`. Без этих правил результат зависел бы от
`core.autocrlf` у каждого разработчика.
