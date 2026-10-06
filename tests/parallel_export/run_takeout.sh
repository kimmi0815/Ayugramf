#!/usr/bin/env bash
set -euo pipefail
test_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$test_root/../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/parallel-export-takeout.XXXXXX")"
trap 'rm -rf "$test_build"' EXIT
python3 "$test_root/extract_takeout_api.py" \
  "${TAKEOUT_API_SOURCE:-$repo_root/Telegram/SourceFiles/export/export_api_wrap.cpp}" \
  "$test_build/takeout_api.inc"
python3 "$test_root/extract_takeout_api.py" --controller-state \
  "$repo_root/Telegram/SourceFiles/export/export_controller.cpp" \
  "$repo_root/Telegram/SourceFiles/export/export_controller.h" \
  "$test_build/takeout_state_types.inc" \
  "$test_build/takeout_state_methods.inc"
"${CXX:-clang++}" \
  -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wno-deprecated-this-capture -Wno-deprecated-literal-operator \
  -DTAKEOUT_SHARED_API \
  -I "$test_build" \
  -I "$test_root/takeout_stubs" \
  -I "$test_root" \
  -I "$repo_root/Telegram/lib_rpl" \
  -I "$repo_root/Telegram/lib_base" \
  -I "$repo_root/Telegram/ThirdParty/GSL/include" \
  -I "$repo_root/Telegram/SourceFiles" \
  -include "$test_root/takeout_domain.h" \
  "${TAKEOUT_SESSION_SOURCE:-$repo_root/Telegram/SourceFiles/export/export_takeout_session.cpp}" \
  "$test_root/takeout_tests.cpp" \
  -o "$test_build/takeout_tests"
"$test_build/takeout_tests" "$@"
