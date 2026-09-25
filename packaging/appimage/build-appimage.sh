#!/usr/bin/env bash
set -euo pipefail

# Build script for Quiver AppImage using linuxdeploy & linuxdeploy-plugin-gtk
export APPIMAGE_EXTRACT_AND_RUN=1
BUILD_DIR="${1:-build-appimage}"
APP_DIR="${BUILD_DIR}/AppDir"

echo "==> Configuring and building Quiver..."
cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "==> Installing into AppDir staging area..."
rm -rf "${APP_DIR}"
DESTDIR="${APP_DIR}" cmake --install "${BUILD_DIR}"

if [ -d /usr/libexec/glycin-loaders ]; then
    mkdir -p "${APP_DIR}/usr/libexec"
    cp -r /usr/libexec/glycin-loaders "${APP_DIR}/usr/libexec/"
fi
if [ -d /usr/share/glycin-loaders ]; then
    mkdir -p "${APP_DIR}/usr/share"
    cp -r /usr/share/glycin-loaders "${APP_DIR}/usr/share/"
fi

echo "==> Downloading linuxdeploy tools..."
mkdir -p "${BUILD_DIR}/tools"
cd "${BUILD_DIR}/tools"
if [ ! -f linuxdeploy-x86_64.AppImage ]; then
    wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi
if [ ! -f linuxdeploy-plugin-gtk.sh ]; then
    wget -q https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh
    chmod +x linuxdeploy-plugin-gtk.sh
fi
cd -

echo "==> Generating AppImage..."
export OUTPUT="Quiver-x86_64.AppImage"
export LINUXDEPLOY_GTK_VERSION=4

"${BUILD_DIR}/tools/linuxdeploy-x86_64.AppImage" \
    --appdir "${APP_DIR}" \
    --desktop-file "${APP_DIR}/usr/share/applications/quiver.desktop" \
    --icon-file "${APP_DIR}/usr/share/icons/hicolor/128x128/apps/quiver-icon-app.png" \
    --plugin gtk \
    --output appimage

echo "==> AppImage created successfully: ${OUTPUT}"
