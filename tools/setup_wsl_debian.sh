#!/usr/bin/env bash
# Настройка WSL Debian для сборки OpenVegas (Qt 6.8+, CMake, Ninja).
# Запуск из WSL:
#   bash tools/setup_wsl_debian.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QT_VERSION="6.8.3"
QT_AQT_ARCH="linux_gcc_64"
QT_DIR="${HOME}/Qt/${QT_VERSION}/gcc_64"
ENV_FILE="${HOME}/.openvegas_wsl_env.sh"

echo "========================================"
echo "OpenVegas — настройка WSL Debian"
echo "========================================"
echo "Проект:  $PROJECT_ROOT"
echo "Qt:      $QT_DIR"
echo

if ! grep -qi debian /etc/os-release 2>/dev/null; then
    echo "[ОШИБКА] Скрипт рассчитан на Debian/Ubuntu в WSL."
    exit 1
fi

echo "[1/5] Системные пакеты..."
if ! sudo -n true 2>/dev/null; then
    echo "sudo требует пароль. Запустите от root в PowerShell:"
    echo "  wsl -d Debian -u root -- bash -lc \"apt-get update && apt-get install -y build-essential cmake ninja-build pkg-config git python3 python3-pip python3-venv libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1-dev libfreetype6-dev libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxrender-dev libxcb1-dev libxcb-glx0-dev libxcb-keysyms1-dev libxcb-image0-dev libxcb-shm0-dev libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev libxcb-xinerama0-dev libxcb-xkb-dev libxcb-cursor0 libdbus-1-dev libpulse-dev libasound2-dev libssl-dev patchelf file\""
    echo "или выполните apt вручную, затем перезапустите скрипт."
    exit 1
fi
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    python3 \
    python3-pip \
    python3-venv \
    libgl1-mesa-dev \
    libegl1-mesa-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-0 \
    libfontconfig1-dev \
    libfreetype6-dev \
    libx11-dev \
    libx11-xcb-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxrender-dev \
    libxcb1-dev \
    libxcb-glx0-dev \
    libxcb-keysyms1-dev \
    libxcb-image0-dev \
    libxcb-shm0-dev \
    libxcb-icccm4-dev \
    libxcb-sync-dev \
    libxcb-xfixes0-dev \
    libxcb-shape0-dev \
    libxcb-randr0-dev \
    libxcb-render-util0-dev \
    libxcb-util-dev \
    libxcb-xinerama0-dev \
    libxcb-xkb-dev \
    libxcb-cursor0 \
    libdbus-1-dev \
    libpulse-dev \
    libasound2-dev \
    libssl-dev \
    patchelf \
    file

echo
echo "[2/5] aqtinstall..."
python3 -m pip install --user --upgrade pip aqtinstall
export PATH="${HOME}/.local/bin:${PATH}"

if [[ ! -f "${QT_DIR}/bin/qmake" ]]; then
    aqt install-qt linux desktop "${QT_VERSION}" "${QT_AQT_ARCH}" \
        -m qtsvg \
        -O "${HOME}/Qt"
else
    echo "Qt ${QT_VERSION} уже установлен в ${QT_DIR}"
fi

echo
echo "[3/5] Файл окружения..."
cat > "${ENV_FILE}" << EOF
#!/usr/bin/env bash
# OpenVegas WSL env

export QT_VERSION="${QT_VERSION}"
export QT_DIR="\${HOME}/Qt/\${QT_VERSION}/gcc_64"

export PATH="\${QT_DIR}/bin:\${HOME}/.local/bin:\${PATH}"
export CMAKE_PREFIX_PATH="\${QT_DIR}\${CMAKE_PREFIX_PATH:+:\${CMAKE_PREFIX_PATH}}"
export QT_PLUGIN_PATH="\${QT_DIR}/plugins"
export QML2_IMPORT_PATH="\${QT_DIR}/qml"
export LD_LIBRARY_PATH="\${QT_DIR}/lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
EOF
chmod +x "${ENV_FILE}"
grep -F "${ENV_FILE}" "${HOME}/.bashrc" >/dev/null 2>&1 || \
    echo "source ${ENV_FILE} 2>/dev/null" >> "${HOME}/.bashrc"
# shellcheck disable=SC1090
source "${ENV_FILE}"

echo
echo "[4/5] Конфигурация CMake..."
cd "$PROJECT_ROOT"
cmake -S . -B build/linux-wsl -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_DIR}"

echo
echo "[5/5] Сборка..."
cmake --build build/linux-wsl --parallel

echo
echo "========================================"
echo "Готово!"
echo "========================================"
echo
echo "Пересборка:"
echo "  cd '$PROJECT_ROOT'"
echo "  source ~/.openvegas_wsl_env.sh"
echo "  cmake --build build/linux-wsl --parallel"
echo
echo "Запуск GUI (Windows 10: нужен VcXsrv/X410; Windows 11: WSLg):"
echo "  source ~/.openvegas_wsl_env.sh"
echo "  ./build/linux-wsl/OpenVegas"
