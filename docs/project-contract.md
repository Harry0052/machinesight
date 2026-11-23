# Project contract

## User task

A maintenance technician has a recorded RPM log from a legacy benchtop display and a second log from an independent reference tachometer. MachineSight reports where the steady readings disagree, where the logs do not overlap, and which samples are unqualified. The technician uses that report to decide what to inspect next. The program does not declare the machine safe, faulty, or in need of shutdown.

## Minimum dependencies

- CMake 3.16 or newer
- A C17 compiler, gcc or clang
- A POSIX shell for the CLI test

No third-party library, network service, or package registry is required to build, test, or run the demo. Fixtures are stored in the repository.

## Supported comparison

Inputs are two CSV logs and a sidecar.

- CSV header: `elapsed_ms,rpm,quality`
- `elapsed_ms`: integer milliseconds, strictly increasing, 0 through 604800000
- `rpm`: non-negative decimal with at most three fractional digits, up to 200000.000
- `quality`: `ok`, `invalid`, or `missing`
- Sidecar: `key=value` text, schema version 1

The sidecar must name both sources, a sampling interval, a validated time offset, an allowed skew, a comparison tolerance, a reference uncertainty, a maximum reference rate, a clock-alignment provenance string, a calibration status, and a time base.

Supported calibration statuses are `synthetic-teaching-fixture` and `reference-stated`. The only supported time base is `elapsed_ms`. Empty provenance, the token `undeclared`, any other calibration status, and any other time base are rejected. The report is not produced in those cases.

Matching adds `validated_time_offset_ms` to each machine timestamp and pairs that machine sample with the nearest unused `ok` reference sample inside `allowed_skew_ms`, inclusive. Machine samples are considered in time order. An equal distance selects the earlier reference timestamp, then the lower source line. A reference sample is used at most once.

A reference sample is dynamic when it shares an adjacent `ok` reference interval whose separation is at most `sampling_interval_ms + allowed_skew_ms` and whose absolute rate is strictly greater than `max_reference_rate_rpm_per_s`. Both samples on that interval are unqualified. Wider gaps are missing evidence, not acceleration. A matched dynamic sample is not a disagreement.

A steady pair disagrees when the absolute RPM difference is strictly greater than `comparison_tolerance_rpm + reference_uncertainty_rpm`. Contiguous disagreeing pairs become one interval only when no other machine sample lies between them and their machine timestamps are at most one sampling window apart.

`valid_pair_coverage` is `valid_steady_pairs / ok_machine_rows`. Valid steady pairs are agrees plus disagrees. Invalid, missing, dynamic, and unmatched samples stay outside that numerator and are counted separately.

## Measurable acceptance case

In a stable 1000 RPM reference segment, three machine samples of 1120 RPM at a common one-second spacing are one sustained disagreement when the tolerance is 25 RPM and the reference allowance is 5 RPM. The threshold is 30 RPM and the absolute difference is 120 RPM. Removing the middle reference sample leaves that timestamp unverified and splits the result into two disagreement intervals.

The frozen demo adds a healthy segment, one invalid row, one explicit missing row, a 500 RPM/s ramp, a reference gap, and one unmatched reference sample. The demo must report coverage `8/13`, three disagreement intervals (the three-sample bias plus the two gap ends), and unverified rows for the ramp, the gap middle, the invalid row, the missing row, and the unused reference.

## Bounds

- Line length: 511 characters
- Field length: 63 characters
- Rows per file: 10000
- File size: 8 MiB
- Embedded NUL bytes, duplicate timestamps, decreasing timestamps, and integers that do not fit in the supported range are rejected
- Writes are limited to `summary.txt`, `findings.csv`, and `intervals.csv` inside the requested output directory

## Out of scope

Live sensors, machine control, automated shutdown, embedded controllers, operating instructions, and any decision that the equipment is safe to run.
