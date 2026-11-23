# Demo oracle

Hand calculation for `fixtures/demo`. The threshold is 25 RPM tolerance plus 5 RPM reference uncertainty, so a steady pair disagrees only when the absolute difference is strictly greater than 30 RPM. The sampling window is 1000 ms plus 100 ms of allowed skew. A reference rate is dynamic only when two adjacent `ok` reference samples are at most 1100 ms apart and the absolute rate is strictly greater than 50 RPM/s.

Source lines count the header as line 1.

## Machine rows

| Line | elapsed_ms | rpm | quality | Result |
| --- | ---: | ---: | --- | --- |
| 2 | 0 | 1000 | ok | Agree with reference line 2. Difference 0. |
| 3 | 1000 | 1000 | ok | Agree with reference line 3. Difference 0. |
| 4 | 2000 | 1010 | ok | Agree with reference line 4. Difference 10, which is not above 30. |
| 5 | 3000 | 1000 | invalid | Counted as invalid. Not compared. |
| 6 | 5000 | 1120 | ok | Disagree with reference line 5. Difference 120. |
| 7 | 6000 | 1120 | ok | Disagree with reference line 6. Difference 120. |
| 8 | 7000 | 1120 | ok | Disagree with reference line 7. Difference 120. |
| 9 | 10000 | 1100 | ok | Matched to reference line 8, then unqualified because that reference is on the ramp. |
| 10 | 11000 | 1600 | ok | Dynamic, reference line 9. |
| 11 | 12000 | 2100 | ok | Dynamic, reference line 10. |
| 12 | 13000 | 2600 | ok | Dynamic, reference line 11. |
| 13 | 16000 | 1120 | ok | Disagree with reference line 12. Difference 120. |
| 14 | 17000 | 1120 | ok | No reference inside 100 ms. Unverified. |
| 15 | 18000 | 1120 | ok | Disagree with reference line 13. Difference 120. |
| 16 | 20000 | 1000 | missing | Counted as missing. Not compared. |

## Reference rate

Reference lines 2–7 are steady: neighboring steps that fall inside 1100 ms change by 0 RPM. The step from 7000 ms to 10000 ms is 3000 ms, so it is a gap rather than a rate.

Reference lines 8–11 are 1000, 1500, 2000, and 2500 RPM at 1000 ms spacing. Each step is 500 RPM/s, which is above 50 RPM/s, so all four samples are dynamic. The following reference sample is 3000 ms later, so the ramp does not mark the 16000 ms sample dynamic.

Reference line 14 at 21000 ms has no machine sample inside the skew. It stays unmatched.

## Counts

- ok machine rows: 13
- valid steady pairs: 3 agrees + 5 disagrees = 8
- coverage: 8/13 = 0.615384..., printed as 0.6153 after truncating to four decimal places
- matched pairs, including the four dynamic matches: 12
- disagreement intervals: 3
  - lines 6–8, one run at 5000–7000 ms
  - line 13 alone, because line 14 is unverified and 2000 ms is wider than the sampling window
  - line 15 alone

Removing the middle reference sample from a three-sample bias is the separate `fixtures/gap` case. The same arithmetic leaves machine line 3 unmatched and produces two one-sample intervals instead of one three-sample interval.
