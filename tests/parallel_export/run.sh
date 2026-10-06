#!/usr/bin/env bash
set -euo pipefail
test_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$test_root/../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/parallel-export-tests.XXXXXX")"
trap 'rm -rf "$test_build"' EXIT
"${CXX:-clang++}" \
  -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wno-deprecated-this-capture -Wno-deprecated-literal-operator \
  -I "$test_root/stubs" \
  -I "$test_root" \
  -I "$repo_root/Telegram/lib_rpl" \
  -I "$repo_root/Telegram/lib_base" \
  -I "$repo_root/Telegram/ThirdParty/GSL/include" \
  -I "$repo_root/Telegram/ThirdParty/range-v3/include" \
  -I "$repo_root/Telegram/SourceFiles" \
  -include "$test_root/prelude.h" \
  "$repo_root/Telegram/SourceFiles/export/export_manager.cpp" \
  "$test_root/fake_domain.cpp" \
  "$test_root/manager_tests.cpp" \
  -o "$test_build/manager_tests"
"$test_build/manager_tests"
