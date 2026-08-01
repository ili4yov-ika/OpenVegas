#!/bin/bash
# Скрипт сборки Debian/Ubuntu пакета (.deb) для OpenVegas
# Требуется: CMake, Qt6, dpkg-buildpackage или debuild

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
DEBIAN_DIR="$SCRIPT_DIR/debian"
PACKAGE_NAME="openvegas"
VERSION="0.1.0"

echo "========================================"
echo "OpenVegas - Debian Package Builder"
echo "========================================"
echo

command -v cmake >/dev/null 2>&1 || { echo "[ОШИБКА] CMake не найден!"; exit 1; }
command -v dpkg-buildpackage >/dev/null 2>&1 || { echo "[ОШИБКА] dpkg-buildpackage не найден! Установите: sudo apt-get install build-essential devscripts"; exit 1; }

echo "[1/5] Подготовка debian файлов..."
if [ ! -d "$PROJECT_ROOT/debian" ]; then
    cp -r "$DEBIAN_DIR" "$PROJECT_ROOT/"
fi

echo "[2/5] Конфигурация CMake..."
cd "$PROJECT_ROOT"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"} \
      "$PROJECT_ROOT"

echo "[3/5] Сборка проекта..."
cmake --build . --config Release

echo "[4/5] Установка в staging..."
DESTDIR="$PROJECT_ROOT/debian-package" cmake --install . --config Release

echo "[5/5] Создание .deb пакета..."
cd "$PROJECT_ROOT"
dpkg-buildpackage -b -us -uc

echo
echo "========================================"
echo "Готово! Пакет создан в: $PROJECT_ROOT/../${PACKAGE_NAME}_${VERSION}-1_amd64.deb"
echo "========================================"
echo
echo "Установка: sudo dpkg -i ${PACKAGE_NAME}_${VERSION}-1_amd64.deb"
echo "Исправление зависимостей: sudo apt-get install -f"
