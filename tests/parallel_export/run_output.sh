#!/usr/bin/env bash
set -euo pipefail
test_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$test_root/../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/parallel-export-output.XXXXXX")"
trap 'rm -rf "$test_build"' EXIT
python3 "$test_root/extract_normalize_path.py" \
  "$repo_root/Telegram/SourceFiles/export/output/export_output_abstract.cpp" \
  "$test_build/normalize_path.cpp"
"${CXX:-clang++}" \
  -std=c++20 -g -O1 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$test_root" \
  "$test_build/normalize_path.cpp" \
  "$test_root/filesystem_domain.cpp" \
  "$test_root/output_tests.cpp" \
  -o "$test_build/output_tests"
"$test_build/output_tests" "$test_build/fixtures"
