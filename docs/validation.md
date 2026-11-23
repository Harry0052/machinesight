# Validation

Access date: 2026-11-23. The checks below use the synthetic fixtures in this repository. They are not a calibration certificate and they do not use a shop measurement.

## Host

- Linux 6.12.113+ x86_64
- gcc 13.3.0
- clang 18.1.3
- CMake 3.28.3

## Acceptance evidence

| Case | Check | Observed |
| --- | --- | --- |
| Planted 120 RPM bias | Three aligned 1120 RPM samples against a 1000 RPM reference, threshold 30 RPM | One interval of three samples. `fixtures/bias` and `tests/test_compare.c`. |
| Middle reference removed | Same three machine samples with the middle reference deleted | Two one-sample intervals and one `unmatched_machine` row. `fixtures/gap`. |
| Clock skew | Offsets -51, -50, 0, 50, 51 ms with skew 50 ms | Matches at the inclusive boundary and not one millisecond outside it. |
| Tie and one-to-one | Equidistant references, then two machine rows and one reference | Earlier reference line wins. The reference is used once. |
| Ramp | 500 RPM/s reference steps, limit 50 RPM/s | Four machine rows are `dynamic_unqualified`. No disagreement interval. A step wider than the sampling window is not treated as a ramp. |
| Invalid and missing | Separate quality values on otherwise aligned rows | Invalid and missing counts stay separate from unmatched and disagree counts. |
| Parser boundaries | Malformed RPM, duplicate timestamp, overflowing integer, embedded NUL, 520-character line, 10001 rows, uncalibrated sidecar, unsupported time base | Rejected with a path, line, and reason. Row and line limits exit 4. Unsupported calibration exits 5. |
| Frozen demo | `fixtures/demo` against `fixtures/demo/ORACLE.md` | Coverage `8/13`, three disagreement intervals, ramp and gap unverified. |
| Two compilers | `bash scripts/compare-compilers.sh` | gcc 13.3.0 and clang 18.1.3 release reports and standard output were identical, and both matched `expected_summary.txt`. |
| Sanitizers | gcc Debug with `-DMACHINESIGHT_SANITIZE=ON` | `ctest` passed, including the demo diff. AddressSanitizer and UndefinedBehaviorSanitizer abort on findings. |

The demo command prints the summary on standard output and writes the same bytes to `summary.txt`. A failing input leaves standard output empty and does not create the output directory.

## Performance budget

The supported demo is a few dozen rows. A technician can wait well under a second, so the budget is 0.5 seconds of wall time for `machinesight demo` and 1.0 second of CPU time for 10000 exact one-to-one pairs.

Observed on this host:

- Demo wall time 0.000709 seconds (`./build/machinesight demo --output /tmp/ms-wall`).
- 10000-pair CPU time 0.001092 seconds with gcc Debug, 0.000786 seconds with clang Release, and 0.002794 seconds with gcc sanitizers.

Both budgets passed. The 10000-row case is an in-memory regression, not a distributed dataset.

## Trust boundary

- Inputs are bounded: 8 MiB files, 10000 rows, 511-character lines, 63-character fields, checked integers, and printable sidecar text.
- RPM is stored as milli-RPM integers. The report does not use binary floating-point formatting.
- The program does not spawn a process or open a network connection.
- Writes are limited to `summary.txt`, `findings.csv`, and `intervals.csv` under the requested directory. Parent directories are created for that path only.
- Unsupported calibration, time base, schema, or missing clock provenance stops the run before a disagreement report is written.
- Disagreement text says the result is not an equipment condition, a safe-to-operate decision, or a shutdown instruction.
- Dependencies are the C compiler and CMake. Both are used under their normal toolchain licenses. The project license is the MIT license already in `LICENSE`. No advisory database was queried because there is no third-party package lockfile to scan. A text search of the source tree found no credential or key material.

## Clean checkout

A copy of the source, without build directories, was configured with the default `cc` (clang 18.1.3), built Release, tested, and run as `./build/machinesight demo --output out/demo`. The summary, findings, and intervals matched the committed oracle files. `out/demo` did not exist before that command.

## Residual limits

- The clock model is one validated offset, not a drift rate.
- Matching walks machine samples in time order and keeps the nearest unused reference. It is deterministic. It is not a global optimal assignment.
- Coverage decimals are truncated toward zero at four places. The fraction is also printed.
- Steady-state looks only at adjacent reference samples inside one sampling window.
- No physical instrument was connected. Field logs are out of scope until their provenance and permission are recorded.
- Clang's sanitizer runtime is not installed on this host. The sanitizer job uses gcc, which provides libasan and libubsan here. GitHub Actions uses that same gcc sanitizer configuration.
