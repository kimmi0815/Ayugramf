#!/usr/bin/env bash
set -euo pipefail
build_repository="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
debug_build_directory="${1:-$build_repository/out/Debug}"
case "$debug_build_directory" in
  /*) ;;
  *) debug_build_directory="$build_repository/$debug_build_directory" ;;
esac
cd "$build_repository"
./Telegram/configure.sh \
  -G Xcode \
  -D CMAKE_CONFIGURATION_TYPES=Debug \
  -D TDESKTOP_DEBUG_OUTPUT_DIRECTORY="$debug_build_directory" \
  -D CMAKE_OSX_ARCHITECTURES=arm64 \
  -D CMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -D CMAKE_CXX_FLAGS=-DMETA_NO_STD_FORWARD_DECLARATIONS=1 \
  -D CMAKE_OBJCXX_FLAGS=-DMETA_NO_STD_FORWARD_DECLARATIONS=1 \
  -D DESKTOP_APP_MAC_ARCH=arm64 \
  -D DESKTOP_APP_USE_PACKAGED=OFF \
  -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON \
  -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON \
  -D DESKTOP_APP_ENABLE_LTO=OFF \
  -D CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
  -D CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
  -D CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY= \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
cmake --build out --config Debug --target Telegram --parallel 8 -- \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY=
codesign --force --deep --sign - \
  --entitlements Telegram/Telegram/Telegram.entitlements \
  "$debug_build_directory/AyuGram.app"
codesign --verify --deep --strict --verbose=2 \
  "$debug_build_directory/AyuGram.app"
mkdir -p "$debug_build_directory/TelegramForcePortable"
