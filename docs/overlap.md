# Overlap review

Access date for every source below: 2026-11-23.

Decision: **proceed**. Build a separate hardware-free MachineSight CLI. No examined project already meets the acceptance case: recover a planted steady 120 RPM bias, break that continuous run when the middle reference sample is removed, leave ramps and unsynchronized spans unverified, and report valid-pair coverage together with calibration and time-base limits.

## Capability matrix

Shared acceptance case used for every row: three one-second samples, reference 1000 RPM, machine 1120 RPM, comparison tolerance 25 RPM, stated reference uncertainty 5 RPM. The case passes only when those three samples form one sustained disagreement and deleting the middle reference sample leaves the middle machine sample unverified instead of paired again. A ramp must stay unqualified.

| Candidate | Primary URL | Version or commit | License | What it already does | Acceptance case | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Arduino non-contact tachometer article | https://blog.arduino.cc/2022/09/23/a-diy-non-contact-digital-tachometer-for-machinists/ | Article dated 2022-09-23. No firmware repository linked from the article. | Blog text. No comparison-tool license. | Describes a battery-powered IR pulse counter that displays RPM on an OLED. A reference workflow could export pulse times and a derived RPM. The article does not define a timestamped log, quality flag, uncertainty, or clock provenance. | Not met. Live display, not a two-log comparison. Hardware construction is outside this MVP. | Documentation |
| NI-DAQmx waveform timestamps | https://knowledge.ni.com/KnowledgeArticleDetails?id=kA00Z000000P9Q9SAK | Article updated 2025-06-13. Driver is NI-DAQmx. | Proprietary NI documentation and driver. | `t0` is a software timestamp from the host clock when the buffer is read, adjusted by hardware `dt` and sample count. `dt` is hardware-timed. Handshake, implicit, on-demand, and change-detection timing return `dt` of 0. The waveform type assumes equal sample spacing. | Not met. Explains why a raw DAQ timestamp is not an independent alignment. It does not compare a legacy RPM log with a reference log. | Documentation |
| UltraLog | https://github.com/ClassicMiniDIY/UltraLog | Commit `81b87de79212d1404e7a8007cde9d21373a08102` on `main` (2026-11-23). | AGPL-3.0 | Desktop log viewer. Two-log compare overlays channels with a manual time offset or cursor alignment. Field names can be normalized (`Engine Speed` with `RPM`). | Not met. No steady-state gate, no tolerance-plus-uncertainty rule, no unverified-gap report, no required calibration status. | Documentation. Not executed: GUI application and AGPL-3.0, and it does not consume the shared fixture as a headless acceptance test. |
| tachorpm | https://github.com/rcgsheffield/tachorpm and https://pypi.org/project/tachorpm/ | Commit `8e92dcf3678a9ff0f0f2003fbdb3a09e3ff1653b` (2026-01-28). README installation is `pip install tachorpm`. | README states MIT. GitHub license metadata was null in the API response, so the SPDX field is unresolved. | Extracts an RPM signal from a tachometer pulse waveform (`tachorpm(x, fs)`), porting the MATLAB Signal Processing Toolbox idea. | Not met. Input is a pulse vector, not a legacy readout log plus a reference log. | Documentation. Not executed: the shared CSV fixture is the wrong input shape, so a run would not test the acceptance metric. |
| OpticalTachometerOledDisplay | https://github.com/tmbarbour/OpticalTachometerOledDisplay | Commit `ff8432ac0978c2af5c12ae21ec92a9c37b38e4fa` (2018-02-04). | MIT, stated in the repository README. | Arduino optical tachometer that displays spindle RPM. | Not met. Nearby maker project found while checking the Arduino article. It is not that 2022 article and it does not compare logs. | Documentation |
| MNE `realign_raw` | https://mne.tools/stable/generated/mne.preprocessing.realign_raw.html | Documentation version 1.13.2. | License file was not re-read in this session. | Estimates a start offset and linear clock drift from shared event times, then resamples one recording onto the other. | Not met. General electrophysiology alignment. It does not apply RPM tolerance, uncertainty, steady-state rate limits, or coverage accounting. | Documentation. Not installed. |
| Naive nearest join | Local Python standard-library probe recorded in `docs/grokbot-progress.md`. | CPython 3 on this machine. | n/a (probe, not a dependency). | Pairs each machine timestamp to the nearest reference with no skew limit, no one-to-one rule, and no steady-state rule. | Failed the shared fixture. With the middle reference removed, the middle machine sample was paired to a reused reference and all three samples still looked like disagreements. A lagging ramp was flagged as disagreement. | Observed run |

GitHub code search for `repo:microsoft/vcpkg tachometer OR rpm` returned no indexed hits. Web search of Conan and vcpkg did not identify a package that compares a legacy RPM readout with an independent reference. `tach` on PyPI is an unrelated Python package linter. MATLAB `tachorpm` is a proprietary pulse-to-RPM function, not this acceptance case.

Other search hits (kart telemetry cross-correlation, ECU data loggers, bearing-fault tachometer cleaning, CO2MPAS time-series guidance) discuss clock alignment or RPM measurement in other domains. None publishes the MachineSight acceptance report.

## Probe outcome

The constant input was the acceptance series above. The expected artifact was one three-sample disagreement, then a broken run after the middle reference was removed, with the ramp left unqualified. The naive join produced three disagreements after the deletion and flagged the ramp. UltraLog, tachorpm, NI-DAQmx, MNE, and the Arduino article were not treated as passes on documentation grounds; they were not given a weaker fixture.

## Uncertainties

- The 2022 Arduino article does not link firmware, so the exact fields that build could log were not observed. The described product is still a live RPM display.
- tachorpm's SPDX metadata and the MNE license file were not re-verified from the upstream license blobs. Neither project's documented behavior meets the acceptance case.
- UltraLog was not installed. Its README is specific about manual offset overlay and does not describe the acceptance report.
- Conan Center was not mirrored locally. The registry search is a web and GitHub-index check, not an installed `conan search`.

## Decision

Proceed with a new repository. Contributing the acceptance behavior to UltraLog would pull an AGPL desktop viewer and a manual visual compare into a technician's local checker, and it still would not encode calibration refusal or unverified gaps. tachorpm remains a pulse decoder and could be a future importer, not the comparison engine.

The MVP hypothesis worth testing is narrower than a general tachometer: given declared clock alignment, skew, tolerance, reference uncertainty, and a maximum reference rate, report contiguous steady disagreements and keep missing, invalid, dynamic, and unsynchronized samples in separate unverified counts.
