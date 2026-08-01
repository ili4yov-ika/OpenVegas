#!/bin/bash
# Скрипт сборки RPM пакета для OpenVegas (Fedora/RHEL)
# Требуется: CMake, Qt6, rpmbuild

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RPM_DIR="$SCRIPT_DIR/rpm"
PACKAGE_NAME="openvegas"
VERSION="0.1.0"
RELEASE="1"

echo "========================================"
echo "OpenVegas - RPM Package Builder"
echo "========================================"
echo

command -v cmake >/dev/null 2>&1 || { echo "[ОШИБКА] CMake не найден!"; exit 1; }
command -v rpmbuild >/dev/null 2>&1 || { echo "[ОШИБКА] rpmbuild не найден! Установите: sudo dnf install rpm-build rpmdevtools"; exit 1; }

echo "[1/6] Настройка RPM build окружения..."
RPMBUILD_DIR="$HOME/rpmbuild"
mkdir -p "$RPMBUILD_DIR"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

echo "[2/6] Создание исходного архива..."
cd "$PROJECT_ROOT/.."
tar --exclude='.git' \
    --exclude='build' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='SAMPLES/node_modules' \
    -czf "$RPMBUILD_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
    "$(basename "$PROJECT_ROOT")"

echo "[3/6] Подготовка spec файла..."
cp "$RPM_DIR/${PACKAGE_NAME}.spec" "$RPMBUILD_DIR/SPECS/"

echo "[4/6] Конфигурация и сборка проекта..."
cd "$PROJECT_ROOT"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"} \
      "$PROJECT_ROOT"

cmake --build . --config Release

echo "[5/6] Создание RPM пакета..."
cd "$RPMBUILD_DIR"
rpmbuild -ba SPECS/${PACKAGE_NAME}.spec \
    --define "_version $VERSION" \
    --define "_release $RELEASE"

echo
echo "========================================"
echo "Готово! RPM пакет создан в:"
echo "  $RPMBUILD_DIR/RPMS/x86_64/${PACKAGE_NAME}-${VERSION}-${RELEASE}.x86_64.rpm"
echo "========================================"
echo
echo "Установка: sudo rpm -ivh ${PACKAGE_NAME}-${VERSION}-${RELEASE}.x86_64.rpm"
echo "Или: sudo dnf install ${PACKAGE_NAME}-${VERSION}-${RELEASE}.x86_64.rpm"
