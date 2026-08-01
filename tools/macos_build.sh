#!/usr/bin/env bash
# Сборка OpenVegas на macOS (CMake Presets + Ninja).
#
# Использование:
#   bash tools/macos_build.sh              # Debug
#   bash tools/macos_build.sh release      # Release
#   bash tools/macos_build.sh debug test   # Debug + ctest (если есть тесты)
#   bash tools/macos_build.sh release deploy  # Release + macdeployqt

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="${HOME}/.openvegas_macos_env.sh"

if [[ -f "$ENV_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$ENV_FILE"
fi

BUILD_TYPE="debug"
RUN_TEST=0
DEPLOY=0

for arg in "$@"; do
    case "$arg" in
        release|Release) BUILD_TYPE="release" ;;
        debug|Debug) BUILD_TYPE="debug" ;;
        test) RUN_TEST=1 ;;
        deploy) DEPLOY=1 ;;
        *)
            echo "Неизвестный аргумент: $arg"
            echo "Использование: $0 [debug|release] [test] [deploy]"
            exit 1
            ;;
    esac
done

PRESET="macos-${BUILD_TYPE}"
BUILD_DIR="${PROJECT_ROOT}/build/macos"

cd "$PROJECT_ROOT"

echo "=== OpenVegas macOS: preset ${PRESET} ==="
cmake --preset "${PRESET}"
cmake --build --preset "${PRESET}" --parallel

APP_BIN="${BUILD_DIR}/OpenVegas"
if [[ ! -x "$APP_BIN" ]]; then
    echo "[ОШИБКА] Исполняемый файл не найден: $APP_BIN"
    exit 1
fi

echo "Собрано: $APP_BIN"

if [[ "$RUN_TEST" -eq 1 ]]; then
    echo "=== CTest (QT_QPA_PLATFORM=offscreen) ==="
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure || true
fi

if [[ "$DEPLOY" -eq 1 ]]; then
    MACDEPLOYQT="$(command -v macdeployqt || true)"
    if [[ -z "$MACDEPLOYQT" && -n "${CMAKE_PREFIX_PATH:-}" ]]; then
        MACDEPLOYQT="${CMAKE_PREFIX_PATH}/bin/macdeployqt"
    fi
    if [[ ! -x "$MACDEPLOYQT" ]]; then
        echo "[ПРЕДУПРЕЖДЕНИЕ] macdeployqt не найден — пропуск deploy"
    else
        APP_DIR="${BUILD_DIR}/OpenVegas.app"
        mkdir -p "${APP_DIR}/Contents/MacOS"
        cp "$APP_BIN" "${APP_DIR}/Contents/MacOS/OpenVegas"
        "$MACDEPLOYQT" "$APP_DIR"
        echo "Развёрнуто: $APP_DIR"
    fi
fi
