#!/bin/bash

# SPDX-FileCopyrightText: 2026 Kaito Udagawa <umireon@kaito.tokyo>
#
# SPDX-License-Identifier: Apache-2.0

# file: scripts/build_obs_macos.sh
# description: Helper script to build the OBS library for macOS.
# author: Kaito Udagawa <umireon@kaito.tokyo>
# version: 1.0.0
# date: 2026-05-27

set -euo pipefail
shopt -s nullglob

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${PLUGIN_BUILD_DIR:=$ROOT_DIR}"
: "${CMAKE_OSX_ARCHITECTURES:=arm64;x86_64}"
: "${CMAKE_OSX_DEPLOYMENT_TARGET:=12.0}"

. "$ROOT_DIR/buildspec.props"

download_prebuilt() {
  mkdir -p "$PLUGIN_BUILD_DIR/.deps/"

  local outfile="$PLUGIN_BUILD_DIR/.deps/${prebuilt_macos_url##*/}"
  local outdir="$PLUGIN_BUILD_DIR/.deps/obs-deps"

  curl -fsSLo "$outfile" "$prebuilt_macos_url"
  printf '%s  %s\n' "$prebuilt_macos_sha256" "$outfile" | shasum -a 256 -c -
  rm -rf "$outdir"
  mkdir -p "$outdir"
  tar -C "$outdir" -xf "$outfile"
}

download_qt6() {
  mkdir -p "$PLUGIN_BUILD_DIR/.deps/"

  local outfile="$PLUGIN_BUILD_DIR/.deps/${qt6_macos_url##*/}"
  local outdir="$PLUGIN_BUILD_DIR/.deps/obs-deps-qt6"

  curl -fsSLo "$outfile" "$qt6_macos_url"
  printf '%s  %s\n' "$qt6_macos_sha256" "$outfile" | shasum -a 256 -c -
  rm -rf "$outdir"
  mkdir -p "$outdir"
  tar -C "$outdir" -xf "$outfile"
}

clone_obs() {
  if [[ -d "$PLUGIN_BUILD_DIR/obs-studio" ]]; then
    git -C "$PLUGIN_BUILD_DIR/obs-studio" fetch --depth 1 origin "$obs_studio_git_tag"
    git -C "$PLUGIN_BUILD_DIR/obs-studio" checkout --force FETCH_HEAD
    git -C "$PLUGIN_BUILD_DIR/obs-studio" clean -fdx
  else
    git clone --depth 1 --branch "$obs_studio_git_tag" https://github.com/obsproject/obs-studio.git "$PLUGIN_BUILD_DIR/obs-studio"
  fi
}

configure_obs() {
  cmake -S "$PLUGIN_BUILD_DIR/obs-studio" \
    -B "$PLUGIN_BUILD_DIR/build_obs" \
    -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="$CMAKE_OSX_ARCHITECTURES" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$CMAKE_OSX_DEPLOYMENT_TARGET" \
    -DOBS_CMAKE_VERSION=3.0.0 \
    -DENABLE_PLUGINS=OFF \
    -DENABLE_FRONTEND=OFF \
    -DOBS_VERSION_OVERRIDE="$obs_studio_git_tag" \
    -DCMAKE_PREFIX_PATH="$PLUGIN_BUILD_DIR/.deps/obs-deps;$PLUGIN_BUILD_DIR/.deps/obs-deps-qt6" \
    -DCMAKE_INSTALL_PREFIX="$PLUGIN_BUILD_DIR/.deps"
}

build_obs() {
  cmake --build "$PLUGIN_BUILD_DIR/build_obs" --target obs-frontend-api --config "$1" --parallel
}

install_obs() {
  cmake --install "$PLUGIN_BUILD_DIR/build_obs" --component Development --config "$1" --prefix "$PLUGIN_BUILD_DIR/.deps/$1"
}

if [[ "$#" -eq 0 ]]; then
  download_prebuilt
  download_qt6
  clone_obs
  configure_obs
  build_obs Debug
  build_obs Release
  install_obs Debug
  install_obs Release
else
  "$@"
fi
