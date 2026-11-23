# Grokbot progress

## P01 — Research overlap and confirm the MVP

**Phase and status:** PASS

**Files changed:**

- `docs/overlap.md`
- `docs/project-contract.md`
- `docs/grokbot-progress.md`

**Commands and results:**

- GitHub repository metadata and commit lookups for UltraLog, tachorpm, and OpticalTachometerOledDisplay. UltraLog commit `81b87de79212d1404e7a8007cde9d21373a08102`, AGPL-3.0. tachorpm commit `8e92dcf3678a9ff0f0f2003fbdb3a09e3ff1653b`. OpticalTachometerOledDisplay commit `ff8432ac0978c2af5c12ae21ec92a9c37b38e4fa`.
- GitHub code search `repo:microsoft/vcpkg tachometer OR rpm` returned 0 hits.
- Web pages recorded in `docs/overlap.md` were fetched on 2026-11-23, including the Arduino tachometer article and the NI-DAQmx waveform timestamp article (updated 2025-06-13).
- Local probe, observed:

```text
FULL all_above_30 True
GAP still three disagreements True unique_refs 2
RAMP_LAG flagged [False, True, True]
```

The probe paired every machine sample to the nearest reference with replacement. After the middle reference was deleted, the middle machine sample reused another reference, so the run did not break. The lagging ramp was flagged. That fails the acceptance metric.

**Artifact paths:**

- `docs/overlap.md`
- `docs/project-contract.md`

**Open issues:**

- tachorpm SPDX metadata and the MNE license file were not re-read from the upstream license blobs. Neither documented behavior meets the acceptance case.
- UltraLog was not executed. The README describes manual overlay only.
- The 2022 Arduino article does not link firmware.

**Next phase:** P02

## P02 — Create the repository and data contracts

**Phase and status:** PASS

**Files changed:**

- `CMakeLists.txt`, `.gitignore`
- `include/machinesight/ms.h`
- `src/parse.c`, `src/main.c`
- `fixtures/demo/`, `fixtures/bias/`, `fixtures/gap/`, `fixtures/invalid/`, `fixtures/MANIFEST.md`
- `README.md`
- `tests/test_parse.c`, `tests/test_cli.sh`

**Commands and results:**

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/machinesight demo --output out/demo
```

The valid demo fixture was accepted and wrote `out/demo/summary.txt`.

```text
./build/machinesight compare \
  --machine fixtures/invalid/malformed.csv \
  --reference fixtures/demo/reference.csv \
  --sidecar fixtures/demo/sidecar.txt \
  --output out/bad
```

Exit 2. Standard error: `fixtures/invalid/malformed.csv:2: rpm is not a decimal with at most 3 fractional digits`. The output directory was not created.

The toolchain is CMake 3.16 or newer and C17 gcc or clang. No third-party package is fetched at build or run time. Fixture checksums and the hand-written origin are in `fixtures/MANIFEST.md`.

**Artifact paths:**

- `fixtures/demo/machine.csv`
- `fixtures/demo/reference.csv`
- `fixtures/demo/sidecar.txt`
- `fixtures/invalid/malformed.csv`
- `fixtures/demo/ORACLE.md`

**Open issues:** None for this gate.

**Next phase:** P03

## P03 — Build the working core

**Phase and status:** PASS

**Files changed:**

- `src/compare.c`, `src/report.c`
- `tests/test_compare.c`
- `fixtures/demo/expected_summary.txt`
- `fixtures/demo/expected_findings.csv`
- `fixtures/demo/expected_intervals.csv`

**Commands and results:**

```text
./build/test_compare /workspace
```

`compare checks passed`. The in-memory proof flagged three 1120 RPM samples against a 1000 RPM reference as one sustained disagreement at a 30 RPM threshold. Removing the middle reference sample produced two intervals and one unmatched machine row. The demo fixture reported `valid_steady_pairs=8`, `ok_machine_rows=13`, and three intervals at machine lines 6–8, 13, and 15.

**Artifact paths:**

- `fixtures/demo/expected_summary.txt`
- `fixtures/demo/ORACLE.md`

**Open issues:** None for this gate.

**Next phase:** P04

## P04 — Ship the user workflow and reports

**Phase and status:** PASS

**Files changed:**

- `src/main.c`
- `README.md`
- `tests/test_cli.sh`

**Commands and results:**

```text
ctest --test-dir build --output-on-failure
```

The `cli` test passed. `machinesight demo --output <new>/nested/demo` created the directory, printed the summary on standard output, and wrote the same bytes to `summary.txt`. `findings.csv` contains `disagree,6,5,5000,5000,5000,0,1120.000,1000.000,120.000,30.000`, which is machine line 6 (`5000,1120.000,ok`) and reference line 5 (`5000,1000.000,ok`).

The malformed compare command printed the line-numbered explanation on standard error, wrote nothing to standard output, and exited 2. `--help` and `--version` exit 0. A compare command missing its log paths exits 1.

**Artifact paths:**

- `out/demo/summary.txt` from the clean-room run recorded in P06
- `fixtures/demo/expected_findings.csv`

**Open issues:** None for this gate.

**Next phase:** P05

## P05 — Verify correctness, security, and performance

**Phase and status:** PASS

**Files changed:**

- `docs/validation.md`
- `tests/test_parse.c` row-limit case
- `scripts/compare-compilers.sh`

**Commands and results:**

- `ctest --test-dir build --output-on-failure`: 3/3 passed.
- gcc Debug with `-DMACHINESIGHT_SANITIZE=ON`: `ctest --test-dir build-san-gcc` 3/3 passed. NUL, overlong line, overflow, duplicate timestamp, and the 10001st row were included.
- `bash scripts/compare-compilers.sh`: `gcc and clang fixture reports are identical`.
- Demo wall time 0.000709 seconds. Budget 0.5 seconds.
- 10000-pair CPU time 0.001092 seconds on gcc Debug. Budget 1.0 second.
- Clean-room copy configured with clang 18.1.3 reproduced `expected_summary.txt`.

Details, the trust-boundary review, and the residual limits are in `docs/validation.md`.

**Artifact paths:**

- `docs/validation.md`

**Open issues:** Clang's AddressSanitizer runtime is not installed on this host. The sanitizer gate uses gcc. No third-party dependency advisory feed applies.

**Next phase:** P06

## P06 — Prepare a reproducible open-source release

**Phase and status:** PASS

**Files changed:**

- `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`
- `docs/data-terms.md`, `fixtures/MANIFEST.md`
- `.github/workflows/ci.yml`

**Commands and results:**

A source copy with build directories removed was configured, built, and tested with the README commands. `./build/machinesight demo --output out/demo` matched `fixtures/demo/expected_summary.txt`, `expected_findings.csv`, and `expected_intervals.csv`.

The workflow file uses `contents: read`, pins `actions/checkout` to `3d3c42e5aac5ba805825da76410c181273ba90b1` and `actions/upload-artifact` to `cf430e030ddbb5b0abf93d22962f4752f3646cd9`, and runs the tests without secrets. A text search of the tree found no credential markers. Hosted CI is still to be observed.

**Artifact paths:**

- `.github/workflows/ci.yml`
- `fixtures/MANIFEST.md`

**Open issues:** GitHub Actions has not run yet. That is the P07 gate.

**Next phase:** P07

## P07 — Publish and verify on GitHub

**Phase and status:** PASS

**Files changed:**

- Annotated tag `v0.1.0` on `746e0fe7e23acc91651201c9ab5ad74ab3bdc7b8`
- `docs/grokbot-progress.md` (this record, committed after the tag)

**Commands and results:**

GitHub Actions run https://github.com/Harry0052/machinesight/actions/runs/45112128873 completed with conclusion `success` on pull request head `746e0fe7e23acc91651201c9ab5ad74ab3bdc7b8`. `gh pr checks 1` reported:

- `gcc-debug` pass
- `clang-release` pass
- `gcc-sanitize` pass
- `gcc-clang-identical` pass

```text
git tag -a v0.1.0 746e0fe7e23acc91651201c9ab5ad74ab3bdc7b8 -m "MachineSight 0.1.0"
git push origin v0.1.0
git ls-remote --tags origin 'refs/tags/v0.1.0*'
```

The remote tag object is `3986a8676b4d98d44da4c82e881b54d97c6ab50b`. It points at `746e0fe7e23acc91651201c9ab5ad74ab3bdc7b8`.

A fresh clone of the public tag was built and run:

```text
git clone --branch v0.1.0 --depth 1 https://github.com/Harry0052/machinesight.git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/machinesight demo --output out/demo
```

The checkout was `746e0fe7e23acc91651201c9ab5ad74ab3bdc7b8`. Standard output contained `valid_pair_coverage=8/13` and `disagreement_intervals=3`, and it matched `fixtures/demo/expected_summary.txt`. The repository is public, the MIT `LICENSE` file is in the tagged tree, and pull request https://github.com/Harry0052/machinesight/pull/1 was not merged.

**Artifact paths:**

- Tag `v0.1.0`
- CI run https://github.com/Harry0052/machinesight/actions/runs/45112128873
- Pull request https://github.com/Harry0052/machinesight/pull/1

**Open issues:** None. The owner merges the pull request.

**Next phase:** None. This is the last phase.
