#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cmake -S "$root" -B "$tmp/gcc" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake -S "$root" -B "$tmp/clang" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
cmake --build "$tmp/gcc"
cmake --build "$tmp/clang"

(
    cd "$root"
    "$tmp/gcc/machinesight" demo --output "$tmp/gcc-out" >"$tmp/gcc-stdout.txt"
    "$tmp/clang/machinesight" demo --output "$tmp/clang-out" >"$tmp/clang-stdout.txt"
)

diff -u "$tmp/gcc-out/summary.txt" "$tmp/clang-out/summary.txt"
diff -u "$tmp/gcc-out/findings.csv" "$tmp/clang-out/findings.csv"
diff -u "$tmp/gcc-out/intervals.csv" "$tmp/clang-out/intervals.csv"
diff -u "$tmp/gcc-stdout.txt" "$tmp/clang-stdout.txt"
diff -u "$root/fixtures/demo/expected_summary.txt" "$tmp/gcc-out/summary.txt"
echo "gcc and clang fixture reports are identical"
