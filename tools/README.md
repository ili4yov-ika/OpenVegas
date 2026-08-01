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

Опционально задайте `CMAKE_PREFIX_PATH` на корень Qt (например `C:\Qt\6.9.3\mingw_64`).

Результат: `tools\OpenVegas_Setup.exe`.

### Linux (Debian/Ubuntu)

```bash
chmod +x tools/build_deb.sh
tools/build_deb.sh
```

Требуется: CMake, Qt6 (`qt6-base-dev`, `qt6-svg-dev`), `dpkg-buildpackage` / `devscripts`.

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

## Утилиты переводов (опционально)

Скрипты `form_translations.py`, `finalize_translations.py`, `apply_remaining_en.py`,
`clean_ts_vanished.py` — помощники для Qt `.ts`/`.qm`, когда в проекте появятся
файлы локализации. Список исходников для `lupdate`: `untranslated_sources.txt`
(при необходимости обновите пути под `src/`).
