#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: test_cli.sh BINARY ROOT" >&2
    exit 2
fi

BIN=$1
ROOT=$2
cd "$ROOT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

help_text=$("$BIN" --help)
grep -q "machinesight compare" <<<"$help_text"
grep -q "safe-to-operate" <<<"$help_text"
version_text=$("$BIN" --version)
grep -q "0.1.0" <<<"$version_text"

"$BIN" demo --output "$tmp/nested/demo" >"$tmp/demo-stdout.txt" 2>"$tmp/demo-stderr.txt"
diff -u "$ROOT/fixtures/demo/expected_summary.txt" "$tmp/nested/demo/summary.txt"
diff -u "$ROOT/fixtures/demo/expected_summary.txt" "$tmp/demo-stdout.txt"
diff -u "$ROOT/fixtures/demo/expected_findings.csv" "$tmp/nested/demo/findings.csv"
diff -u "$ROOT/fixtures/demo/expected_intervals.csv" "$tmp/nested/demo/intervals.csv"
grep -q "report_directory=$tmp/nested/demo" "$tmp/demo-stderr.txt"
grep -q "disagree,6,5,5000,5000,5000,0,1120.000,1000.000,120.000,30.000" "$tmp/nested/demo/findings.csv"
[[ $(sed -n '6p' "$ROOT/fixtures/demo/machine.csv") == "5000,1120.000,ok" ]]
[[ $(sed -n '5p' "$ROOT/fixtures/demo/reference.csv") == "5000,1000.000,ok" ]]

"$BIN" compare \
    --machine "$ROOT/fixtures/bias/machine.csv" \
    --reference "$ROOT/fixtures/bias/reference.csv" \
    --sidecar "$ROOT/fixtures/bias/sidecar.txt" \
    --output "$tmp/bias" >"$tmp/bias.txt"
grep -q "disagreement_intervals=1" "$tmp/bias.txt"
grep -q "samples=3" "$tmp/bias.txt"

"$BIN" compare \
    --machine "$ROOT/fixtures/gap/machine.csv" \
    --reference "$ROOT/fixtures/gap/reference.csv" \
    --sidecar "$ROOT/fixtures/gap/sidecar.txt" \
    --output "$tmp/gap" >"$tmp/gap.txt"
grep -q "disagreement_intervals=2" "$tmp/gap.txt"
grep -q "reason=unmatched_machine elapsed_ms=1000" "$tmp/gap.txt"

expect_code() {
    local want=$1
    shift
    set +e
    "$@" >"$tmp/out.txt" 2>"$tmp/err.txt"
    local code=$?
    set -e
    if [[ $code -ne $want ]]; then
        echo "expected exit $want, got $code" >&2
        echo "command: $*" >&2
        cat "$tmp/err.txt" >&2
        exit 1
    fi
    if [[ -s "$tmp/out.txt" ]]; then
        echo "failing command wrote a report to stdout" >&2
        exit 1
    fi
}

expect_code 2 "$BIN" compare \
    --machine "$ROOT/fixtures/invalid/malformed.csv" \
    --reference "$ROOT/fixtures/demo/reference.csv" \
    --sidecar "$ROOT/fixtures/demo/sidecar.txt" \
    --output "$tmp/bad-malformed"
grep -q "rpm is not a decimal" "$tmp/err.txt"
[[ ! -d "$tmp/bad-malformed" ]]

expect_code 2 "$BIN" compare \
    --machine "$ROOT/fixtures/invalid/duplicate.csv" \
    --reference "$ROOT/fixtures/demo/reference.csv" \
    --sidecar "$ROOT/fixtures/demo/sidecar.txt" \
    --output "$tmp/bad-duplicate"
grep -q "duplicate timestamp" "$tmp/err.txt"

expect_code 2 "$BIN" compare \
    --machine "$ROOT/fixtures/invalid/overflow.csv" \
    --reference "$ROOT/fixtures/demo/reference.csv" \
    --sidecar "$ROOT/fixtures/demo/sidecar.txt" \
    --output "$tmp/bad-overflow"
grep -q "overflow" "$tmp/err.txt"

expect_code 5 "$BIN" compare \
    --machine "$ROOT/fixtures/demo/machine.csv" \
    --reference "$ROOT/fixtures/demo/reference.csv" \
    --sidecar "$ROOT/fixtures/invalid/uncalibrated.txt" \
    --output "$tmp/bad-cal"
grep -q "unsupported calibration_status" "$tmp/err.txt"

python3 - "$tmp/long.csv" <<'PY'
import sys
path = sys.argv[1]
with open(path, "w", encoding="ascii") as handle:
    handle.write("elapsed_ms,rpm,quality\n")
    handle.write("a" * 520 + "\n")
PY
expect_code 4 "$BIN" compare \
    --machine "$tmp/long.csv" \
    --reference "$ROOT/fixtures/demo/reference.csv" \
    --sidecar "$ROOT/fixtures/demo/sidecar.txt" \
    --output "$tmp/bad-long"
grep -q "exceeds 511 characters" "$tmp/err.txt"

expect_code 1 "$BIN" compare --output "$tmp/usage"
grep -q "requires --machine" "$tmp/err.txt"

echo "cli checks passed"
