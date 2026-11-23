# Changelog

## 0.1.0

- First working release of the local RPM comparison CLI.
- Compares a legacy machine log with an independent reference log using a declared clock offset, skew, tolerance, reference uncertainty, and steady-state rate limit.
- Ships synthetic fixtures for a healthy segment, a 120 RPM bias, a ramp, a reference gap, and rejected inputs.
- Runs the fixture demo and sanitizer checks in GitHub Actions.
