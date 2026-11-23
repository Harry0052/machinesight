#include "machinesight/ms.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int supported_calibration(const char *text) {
    return strcmp(text, "synthetic-teaching-fixture") == 0 || strcmp(text, "reference-stated") == 0;
}

static int text_ok(const char *text) {
    size_t n = strlen(text);
    if (n == 0U || n >= (size_t)MS_TEXT_CAP) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32U || c > 126U) {
            return 0;
        }
    }
    return 1;
}

MsStatus ms_validate_config(const MsConfig *cfg, MsError *err) {
    if (cfg == NULL) {
        ms_set_error(err, NULL, 0, "configuration is missing");
        return MS_ERR_INTERNAL;
    }
    if (cfg->schema_version != 1) {
        ms_set_error(err, NULL, 0, "unsupported schema_version; supported value is 1");
        return MS_ERR_UNSUPPORTED;
    }
    if (cfg->sampling_interval_ms < 1 || cfg->sampling_interval_ms > MS_MAX_SAMPLING_MS) {
        ms_set_error(err, NULL, 0, "sampling_interval_ms is outside the supported range");
        return MS_ERR_INPUT;
    }
    if (cfg->validated_time_offset_ms < -MS_MAX_OFFSET_MS || cfg->validated_time_offset_ms > MS_MAX_OFFSET_MS) {
        ms_set_error(err, NULL, 0, "validated_time_offset_ms is outside the supported range");
        return MS_ERR_INPUT;
    }
    if (cfg->allowed_skew_ms < 0 || cfg->allowed_skew_ms > MS_MAX_SKEW_MS) {
        ms_set_error(err, NULL, 0, "allowed_skew_ms is outside the supported range");
        return MS_ERR_INPUT;
    }
    if (cfg->reference_uncertainty_milli_rpm < 0 || cfg->reference_uncertainty_milli_rpm > MS_MAX_RPM_MILLI ||
        cfg->comparison_tolerance_milli_rpm < 0 || cfg->comparison_tolerance_milli_rpm > MS_MAX_RPM_MILLI) {
        ms_set_error(err, NULL, 0, "tolerance or reference uncertainty is outside the supported range");
        return MS_ERR_INPUT;
    }
    if (cfg->comparison_tolerance_milli_rpm > INT64_MAX - cfg->reference_uncertainty_milli_rpm) {
        ms_set_error(err, NULL, 0, "tolerance plus reference uncertainty overflows");
        return MS_ERR_INPUT;
    }
    if (cfg->max_reference_rate_milli_rpm_per_s < 0 ||
        cfg->max_reference_rate_milli_rpm_per_s > MS_MAX_RATE_MILLI_PER_S) {
        ms_set_error(err, NULL, 0, "max_reference_rate_rpm_per_s is outside the supported range");
        return MS_ERR_INPUT;
    }
    if (!text_ok(cfg->machine_source_id) || !text_ok(cfg->reference_source_id)) {
        ms_set_error(err, NULL, 0, "source ids must be printable and non-empty");
        return MS_ERR_INPUT;
    }
    if (!text_ok(cfg->clock_alignment_provenance) || strcmp(cfg->clock_alignment_provenance, "undeclared") == 0) {
        ms_set_error(err, NULL, 0,
                     "clock alignment provenance is required; refusing to compare without an explicit "
                     "validated time offset source");
        return MS_ERR_UNSUPPORTED;
    }
    if (!supported_calibration(cfg->calibration_status)) {
        ms_set_error(err, NULL, 0,
                     "unsupported calibration_status '%s'; supported values are synthetic-teaching-fixture "
                     "and reference-stated",
                     cfg->calibration_status);
        return MS_ERR_UNSUPPORTED;
    }
    if (strcmp(cfg->time_base, "elapsed_ms") != 0) {
        ms_set_error(err, NULL, 0, "unsupported time_base '%s'; supported value is elapsed_ms", cfg->time_base);
        return MS_ERR_UNSUPPORTED;
    }
    if (cfg->sampling_interval_ms > INT64_MAX - cfg->allowed_skew_ms) {
        ms_set_error(err, NULL, 0, "sampling interval plus skew overflows");
        return MS_ERR_INPUT;
    }
    return MS_OK;
}

static MsStatus validate_samples(const char *which, const MsSample *samples, size_t count, MsError *err) {
    if (count > (size_t)MS_MAX_ROWS) {
        ms_set_error(err, which, 0, "row limit of %d exceeded", MS_MAX_ROWS);
        return MS_ERR_LIMIT;
    }
    int64_t previous = 0;
    for (size_t i = 0; i < count; i++) {
        const MsSample *sample = &samples[i];
        if (sample->quality != MS_QUALITY_OK && sample->quality != MS_QUALITY_INVALID &&
            sample->quality != MS_QUALITY_MISSING) {
            ms_set_error(err, which, sample->line, "quality is not ok, invalid, or missing");
            return MS_ERR_INPUT;
        }
        if (sample->line <= 0) {
            ms_set_error(err, which, 0, "sample line id must be positive");
            return MS_ERR_INPUT;
        }
        if (sample->elapsed_ms < 0 || sample->elapsed_ms > MS_MAX_ELAPSED_MS) {
            ms_set_error(err, which, sample->line, "elapsed_ms is outside the supported range");
            return MS_ERR_INPUT;
        }
        if (sample->milli_rpm < 0 || sample->milli_rpm > MS_MAX_RPM_MILLI) {
            ms_set_error(err, which, sample->line, "rpm is outside the supported range");
            return MS_ERR_INPUT;
        }
        if (i > 0U && sample->elapsed_ms == previous) {
            ms_set_error(err, which, sample->line, "duplicate timestamp");
            return MS_ERR_INPUT;
        }
        if (i > 0U && sample->elapsed_ms < previous) {
            ms_set_error(err, which, sample->line, "timestamps must be strictly increasing");
            return MS_ERR_INPUT;
        }
        previous = sample->elapsed_ms;
    }
    return MS_OK;
}

static void count_qualities(const MsSample *samples, size_t count, size_t *ok, size_t *invalid, size_t *missing) {
    *ok = 0;
    *invalid = 0;
    *missing = 0;
    for (size_t i = 0; i < count; i++) {
        if (samples[i].quality == MS_QUALITY_OK) {
            *ok += 1U;
        } else if (samples[i].quality == MS_QUALITY_INVALID) {
            *invalid += 1U;
        } else if (samples[i].quality == MS_QUALITY_MISSING) {
            *missing += 1U;
        }
    }
}

static int rate_exceeds(int64_t delta_milli, int64_t dt_ms, int64_t max_milli_per_s) {
    if (dt_ms <= 0) {
        return 0;
    }
    if (delta_milli < 0) {
        delta_milli = -delta_milli;
    }
    if (delta_milli > INT64_MAX / 1000) {
        return 1;
    }
    int64_t left = delta_milli * 1000;
    if (max_milli_per_s > 0 && dt_ms > INT64_MAX / max_milli_per_s) {
        return 0;
    }
    return left > max_milli_per_s * dt_ms;
}

static void mark_dynamic(const MsConfig *cfg, const MsSample *reference, size_t count, unsigned char *dynamic) {
    size_t previous = SIZE_MAX;
    int64_t window = cfg->sampling_interval_ms + cfg->allowed_skew_ms;
    for (size_t i = 0; i < count; i++) {
        if (reference[i].quality != MS_QUALITY_OK) {
            continue;
        }
        if (previous != SIZE_MAX) {
            int64_t dt = reference[i].elapsed_ms - reference[previous].elapsed_ms;
            if (dt > 0 && dt <= window) {
                int64_t delta = reference[i].milli_rpm - reference[previous].milli_rpm;
                if (rate_exceeds(delta, dt, cfg->max_reference_rate_milli_rpm_per_s)) {
                    dynamic[previous] = 1U;
                    dynamic[i] = 1U;
                }
            }
        }
        previous = i;
    }
}

static void consider_reference(const MsSample *reference, size_t index, const unsigned char *used,
                               int64_t aligned, int64_t skew, size_t *best, int64_t *best_dist) {
    if (reference->quality != MS_QUALITY_OK || used[index] != 0U) {
        return;
    }
    int64_t dist = aligned - reference->elapsed_ms;
    if (dist < 0) {
        dist = -dist;
    }
    if (dist > skew) {
        return;
    }
    if (*best == SIZE_MAX || dist < *best_dist) {
        *best = index;
        *best_dist = dist;
    }
}

static size_t nearest_reference(const MsSample *reference, size_t count, const unsigned char *used,
                                int64_t aligned, int64_t skew) {
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (reference[mid].elapsed_ms < aligned) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }

    size_t best = SIZE_MAX;
    int64_t best_dist = INT64_MAX;
    size_t i = lo;
    while (i > 0U) {
        i -= 1U;
        int64_t dist = aligned - reference[i].elapsed_ms;
        if (dist < 0) {
            dist = -dist;
        }
        if (dist > skew) {
            break;
        }
        consider_reference(&reference[i], i, used, aligned, skew, &best, &best_dist);
    }
    for (size_t j = lo; j < count; j++) {
        int64_t dist = aligned - reference[j].elapsed_ms;
        if (dist < 0) {
            dist = -dist;
        }
        if (dist > skew) {
            break;
        }
        consider_reference(&reference[j], j, used, aligned, skew, &best, &best_dist);
    }
    return best;
}

static void fill_machine(MsFinding *finding, const MsSample *sample) {
    finding->has_machine = 1;
    finding->machine_line = sample->line;
    finding->machine_elapsed_ms = sample->elapsed_ms;
    finding->machine_milli_rpm = sample->milli_rpm;
}

static void fill_reference(MsFinding *finding, const MsSample *sample) {
    finding->has_reference = 1;
    finding->reference_line = sample->line;
    finding->reference_elapsed_ms = sample->elapsed_ms;
    finding->reference_milli_rpm = sample->milli_rpm;
}

static void add_delta(MsFinding *finding, int64_t machine_milli, int64_t reference_milli, int64_t threshold) {
    int64_t delta = machine_milli - reference_milli;
    if (delta < 0) {
        delta = -delta;
    }
    finding->has_delta = 1;
    finding->abs_delta_milli = delta;
    finding->threshold_milli = threshold;
}

static int add_i64(int64_t left, int64_t right, int64_t *out) {
    if (right > 0 && left > INT64_MAX - right) {
        return -1;
    }
    if (right < 0 && left < INT64_MIN - right) {
        return -1;
    }
    *out = left + right;
    return 0;
}

static void build_intervals(const MsConfig *cfg, MsReport *report) {
    size_t capacity = report->counts.disagree_samples;
    if (capacity == 0U) {
        report->intervals = NULL;
        report->interval_count = 0;
        report->counts.disagreement_intervals = 0;
        return;
    }
    MsInterval *intervals = calloc(capacity, sizeof(*intervals));
    if (intervals == NULL) {
        report->intervals = NULL;
        report->interval_count = 0;
        report->counts.disagreement_intervals = 0;
        return;
    }
    size_t count = 0;
    size_t previous = SIZE_MAX;
    int64_t window = cfg->sampling_interval_ms + cfg->allowed_skew_ms;
    for (size_t i = 0; i < report->finding_count; i++) {
        const MsFinding *finding = &report->findings[i];
        if (finding->disposition != MS_DISP_DISAGREE) {
            continue;
        }
        int extend = 0;
        if (previous != SIZE_MAX) {
            int between = 0;
            for (size_t k = previous + 1U; k < i; k++) {
                if (report->findings[k].has_machine) {
                    between = 1;
                    break;
                }
            }
            int64_t dt = finding->machine_elapsed_ms - report->findings[previous].machine_elapsed_ms;
            if (!between && dt > 0 && dt <= window) {
                extend = 1;
            }
        }
        if (!extend) {
            MsInterval *interval = &intervals[count];
            interval->sample_count = 1;
            interval->start_machine_line = finding->machine_line;
            interval->end_machine_line = finding->machine_line;
            interval->start_reference_line = finding->reference_line;
            interval->end_reference_line = finding->reference_line;
            interval->start_elapsed_ms = finding->machine_elapsed_ms;
            interval->end_elapsed_ms = finding->machine_elapsed_ms;
            interval->min_abs_delta_milli = finding->abs_delta_milli;
            interval->max_abs_delta_milli = finding->abs_delta_milli;
            count += 1U;
        } else {
            MsInterval *interval = &intervals[count - 1U];
            interval->sample_count += 1;
            interval->end_machine_line = finding->machine_line;
            interval->end_reference_line = finding->reference_line;
            interval->end_elapsed_ms = finding->machine_elapsed_ms;
            if (finding->abs_delta_milli < interval->min_abs_delta_milli) {
                interval->min_abs_delta_milli = finding->abs_delta_milli;
            }
            if (finding->abs_delta_milli > interval->max_abs_delta_milli) {
                interval->max_abs_delta_milli = finding->abs_delta_milli;
            }
        }
        previous = i;
    }
    report->intervals = intervals;
    report->interval_count = count;
    report->counts.disagreement_intervals = count;
}

MsStatus ms_compare(const MsConfig *cfg, const MsSample *machine, size_t machine_count,
                    const MsSample *reference, size_t reference_count, MsReport *report, MsError *err) {
    if (report == NULL) {
        ms_set_error(err, NULL, 0, "internal compare call is missing a report");
        return MS_ERR_INTERNAL;
    }
    memset(report, 0, sizeof(*report));
    MsStatus st = ms_validate_config(cfg, err);
    if (st != MS_OK) {
        return st;
    }
    if ((machine_count > 0U && machine == NULL) || (reference_count > 0U && reference == NULL)) {
        ms_set_error(err, NULL, 0, "internal compare call is missing samples");
        return MS_ERR_INTERNAL;
    }
    st = validate_samples("machine", machine, machine_count, err);
    if (st != MS_OK) {
        return st;
    }
    st = validate_samples("reference", reference, reference_count, err);
    if (st != MS_OK) {
        return st;
    }

    int64_t threshold = cfg->comparison_tolerance_milli_rpm + cfg->reference_uncertainty_milli_rpm;
    report->threshold_milli_rpm = threshold;
    report->counts.machine_rows = machine_count;
    report->counts.reference_rows = reference_count;
    count_qualities(machine, machine_count, &report->counts.ok_machine_rows, &report->counts.invalid_machine_rows,
                    &report->counts.missing_machine_rows);
    count_qualities(reference, reference_count, &report->counts.ok_reference_rows,
                    &report->counts.invalid_reference_rows, &report->counts.missing_reference_rows);

    unsigned char *dynamic = NULL;
    unsigned char *used = NULL;
    if (reference_count > 0U) {
        dynamic = calloc(reference_count, 1U);
        used = calloc(reference_count, 1U);
        if (dynamic == NULL || used == NULL) {
            free(dynamic);
            free(used);
            ms_set_error(err, NULL, 0, "out of memory while comparing samples");
            return MS_ERR_INTERNAL;
        }
        mark_dynamic(cfg, reference, reference_count, dynamic);
    }

    size_t finding_cap = machine_count + reference_count;
    MsFinding *findings = NULL;
    if (finding_cap > 0U) {
        findings = calloc(finding_cap, sizeof(*findings));
        if (findings == NULL) {
            free(dynamic);
            free(used);
            ms_set_error(err, NULL, 0, "out of memory while comparing samples");
            return MS_ERR_INTERNAL;
        }
    }
    size_t finding_count = 0;

    for (size_t i = 0; i < machine_count; i++) {
        const MsSample *sample = &machine[i];
        MsFinding *finding = &findings[finding_count];
        memset(finding, 0, sizeof(*finding));
        fill_machine(finding, sample);
        if (sample->quality == MS_QUALITY_INVALID) {
            finding->disposition = MS_DISP_INVALID_MACHINE;
            finding_count += 1U;
            continue;
        }
        if (sample->quality == MS_QUALITY_MISSING) {
            finding->disposition = MS_DISP_MISSING_MACHINE;
            finding_count += 1U;
            continue;
        }

        int64_t aligned = 0;
        if (add_i64(sample->elapsed_ms, cfg->validated_time_offset_ms, &aligned) != 0) {
            free(findings);
            free(dynamic);
            free(used);
            ms_set_error(err, "machine", sample->line, "aligned timestamp overflows");
            return MS_ERR_INPUT;
        }
        finding->has_alignment = 1;
        finding->aligned_machine_ms = aligned;
        size_t match = nearest_reference(reference, reference_count, used, aligned, cfg->allowed_skew_ms);
        if (match == SIZE_MAX) {
            finding->disposition = MS_DISP_UNMATCHED_MACHINE;
            report->counts.unmatched_machine += 1U;
            finding_count += 1U;
            continue;
        }
        used[match] = 1U;
        fill_reference(finding, &reference[match]);
        finding->offset_ms = aligned - reference[match].elapsed_ms;
        add_delta(finding, sample->milli_rpm, reference[match].milli_rpm, threshold);
        report->counts.matched_pairs += 1U;
        if (dynamic[match] != 0U) {
            finding->disposition = MS_DISP_DYNAMIC;
            report->counts.dynamic_unqualified_machine += 1U;
        } else if (finding->abs_delta_milli > threshold) {
            finding->disposition = MS_DISP_DISAGREE;
            report->counts.disagree_samples += 1U;
            report->counts.valid_steady_pairs += 1U;
        } else {
            finding->disposition = MS_DISP_AGREE;
            report->counts.agree_pairs += 1U;
            report->counts.valid_steady_pairs += 1U;
        }
        finding_count += 1U;
    }

    for (size_t i = 0; i < reference_count; i++) {
        const MsSample *sample = &reference[i];
        int emit = 0;
        MsDisposition disposition = MS_DISP_UNMATCHED_REFERENCE;
        if (sample->quality == MS_QUALITY_INVALID) {
            emit = 1;
            disposition = MS_DISP_INVALID_REFERENCE;
        } else if (sample->quality == MS_QUALITY_MISSING) {
            emit = 1;
            disposition = MS_DISP_MISSING_REFERENCE;
        } else if (used == NULL || used[i] == 0U) {
            emit = 1;
            disposition = MS_DISP_UNMATCHED_REFERENCE;
            report->counts.unmatched_reference += 1U;
        }
        if (!emit) {
            continue;
        }
        MsFinding *finding = &findings[finding_count];
        memset(finding, 0, sizeof(*finding));
        finding->disposition = disposition;
        fill_reference(finding, sample);
        finding_count += 1U;
    }

    free(dynamic);
    free(used);
    report->findings = findings;
    report->finding_count = finding_count;
    build_intervals(cfg, report);
    if (report->counts.disagree_samples > 0U && report->intervals == NULL) {
        ms_report_free(report);
        ms_set_error(err, NULL, 0, "out of memory while building disagreement intervals");
        return MS_ERR_INTERNAL;
    }
    return MS_OK;
}
