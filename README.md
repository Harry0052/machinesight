# MachineSight

MachineSight compares a recorded legacy machine RPM log with an independent reference tachometer log. It reports steady intervals where the readings disagree, and it keeps missing, invalid, dynamic, and unsynchronized samples unverified.

The person using it is a maintenance technician checking whether a benchtop display agrees with a separate sensor. The report is evidence for the next inspection. It is not a statement that equipment is safe, faulty, or ready to run, and it does not operate a machine.

## Prerequisites

- CMake 3.16 or newer
- A C17 compiler: gcc or clang
- A POSIX shell for the CLI test

There are no third-party libraries and the demo does not download data.

## Install, test, and demo

From a clean checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/machinesight demo --output out/demo
```

The demo reads `fixtures/demo` and writes three files in `out/demo`:

- `summary.txt`, also printed on standard output
- `findings.csv`, one row for every machine sample and every unused reference sample
- `intervals.csv`, contiguous steady disagreements

Diagnostics, including `report_directory=...`, go to standard error.

### Expected demo result

The frozen demo finds every planted steady disagreement and leaves the other spans unverified:

- `valid_pair_coverage=8/13` (`valid_pair_coverage_decimal=0.6153`)
- `disagreement_intervals=3`
- interval 1 is the planted 120 RPM bias on machine lines 6–8 and reference lines 5–7
- intervals 2 and 3 are the single samples on either side of the reference gap
- the 500 RPM/s ramp, the gap at 17000 ms, the invalid row, the missing row, and the unused reference at 21000 ms stay unverified

The hand calculation is in `fixtures/demo/ORACLE.md`. A byte-for-byte copy of the report is stored next to the fixture.

### Trace one finding

`findings.csv` contains:

```text
disagree,6,5,5000,5000,5000,0,1120.000,1000.000,120.000,30.000
```

Machine line 6 of `fixtures/demo/machine.csv` is `5000,1120.000,ok`. Reference line 5 of `fixtures/demo/reference.csv` is `5000,1000.000,ok`. The absolute difference is 120 RPM. The sidecar sets a 25 RPM tolerance and a 5 RPM reference allowance, so the threshold is 30 RPM.

## Compare another pair of logs

```sh
./build/machinesight compare \
  --machine fixtures/bias/machine.csv \
  --reference fixtures/bias/reference.csv \
  --sidecar fixtures/bias/sidecar.txt \
  --output out/bias
```

`fixtures/bias` is the three-sample 1120-versus-1000 proof case and reports one interval. `fixtures/gap` removes the middle reference sample. That middle machine row becomes `unmatched_machine` and the disagreement splits into two intervals.

A rejected file names the path, the line, and the field to fix. No report directory is created.

```sh
./build/machinesight compare \
  --machine fixtures/invalid/malformed.csv \
  --reference fixtures/demo/reference.csv \
  --sidecar fixtures/demo/sidecar.txt \
  --output out/bad
```

That command exits 2 and prints `rpm is not a decimal with at most 3 fractional digits`. `fixtures/invalid/uncalibrated.txt` exits 5 because the calibration status is not one of the two supported values.

## Inputs

Both logs use the header `elapsed_ms,rpm,quality`. Time is integer elapsed milliseconds. RPM has at most three fractional digits. Quality is `ok`, `invalid`, or `missing`. The sidecar is `key=value` text and must include source ids, the sampling interval, a validated time offset, the allowed skew, tolerance, reference uncertainty, the maximum reference rate, clock-alignment provenance, calibration status, and time base.

Supported calibration statuses are `synthetic-teaching-fixture` and `reference-stated`. The only supported time base is `elapsed_ms`. Empty or `undeclared` clock provenance is rejected. The comparison rules, tie break, and bounds are specified in `docs/project-contract.md`.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Analysis completed. Disagreements may still be present. |
| 1 | Usage error. |
| 2 | Input rejected. |
| 3 | Output error. |
| 4 | Line, row, or file limit exceeded. |
| 5 | Unsupported calibration, time base, schema, or clock provenance. |

`machinesight --help` and `machinesight --version` print to standard output and exit 0.

## Data and hardware

Every CSV in `fixtures/` was written for this repository. They are teaching numbers, not shop captures. Terms, checksums, and the generation note are in `docs/data-terms.md` and `fixtures/MANIFEST.md`.

There is no live sensor, controller, or hardware demo. A physical tachometer can be used only by exporting a log that already matches this contract, with its own calibration status and clock provenance filled in by the person who measured it.

## Development

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DMACHINESIGHT_SANITIZE=ON
cmake --build build-san
ctest --test-dir build-san --output-on-failure
bash scripts/compare-compilers.sh
```

The sanitizer build enables AddressSanitizer and UndefinedBehaviorSanitizer and does not recover from those errors. `scripts/compare-compilers.sh` builds gcc and clang release binaries and diffs the demo report. GitHub Actions runs the same checks.

## Contributing

See `CONTRIBUTING.md`. Security reports go through `SECURITY.md`.

## License

MIT. See `LICENSE`.
