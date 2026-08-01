#!/usr/bin/env bash
# OpenVegas WSL env (хранится в WSL home, не на /mnt/d)

export QT_VERSION="6.8.3"
export QT_DIR="${HOME}/Qt/${QT_VERSION}/gcc_64"

export PATH="${QT_DIR}/bin:${HOME}/.local/bin:${PATH}"
export CMAKE_PREFIX_PATH="${QT_DIR}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export QT_PLUGIN_PATH="${QT_DIR}/plugins"
export QML2_IMPORT_PATH="${QT_DIR}/qml"
export LD_LIBRARY_PATH="${QT_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
