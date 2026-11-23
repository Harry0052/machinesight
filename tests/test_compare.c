#include "machinesight/ms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failed = 0;

static void expect_true(const char *name, int cond) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", name);
        g_failed++;
    }
}

static void fill_cfg(MsConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->schema_version = 1;
    cfg->sampling_interval_ms = 1000;
    cfg->validated_time_offset_ms = 0;
    cfg->allowed_skew_ms = 100;
    cfg->reference_uncertainty_milli_rpm = 5000;
    cfg->comparison_tolerance_milli_rpm = 25000;
    cfg->max_reference_rate_milli_rpm_per_s = 50000;
    snprintf(cfg->machine_source_id, sizeof cfg->machine_source_id, "machine");
    snprintf(cfg->reference_source_id, sizeof cfg->reference_source_id, "reference");
    snprintf(cfg->clock_alignment_provenance, sizeof cfg->clock_alignment_provenance, "test-declared-offset");
    snprintf(cfg->calibration_status, sizeof cfg->calibration_status, "synthetic-teaching-fixture");
    snprintf(cfg->time_base, sizeof cfg->time_base, "elapsed_ms");
}

static MsSample sample_of(int64_t line, int64_t elapsed, int64_t milli, MsQuality quality) {
    MsSample sample;
    sample.line = line;
    sample.elapsed_ms = elapsed;
    sample.milli_rpm = milli;
    sample.quality = quality;
    return sample;
}

static MsStatus compare_of(const MsConfig *cfg, const MsSample *machine, size_t mc, const MsSample *reference,
                           size_t rc, MsReport *report) {
    MsError err;
    memset(&err, 0, sizeof err);
    memset(report, 0, sizeof(*report));
    MsStatus st = ms_compare(cfg, machine, mc, reference, rc, report, &err);
    if (st != MS_OK) {
        fprintf(stderr, "compare failed: %s\n", err.message);
    }
    return st;
}

static void test_bias_and_gap(void) {
    MsConfig cfg;
    fill_cfg(&cfg);
    MsSample machine[3];
    MsSample reference[3];
    for (int i = 0; i < 3; i++) {
        machine[i] = sample_of(i + 2, (int64_t)i * 1000, 1120000, MS_QUALITY_OK);
        reference[i] = sample_of(i + 2, (int64_t)i * 1000, 1000000, MS_QUALITY_OK);
    }
    MsReport report;
    MsStatus st = compare_of(&cfg, machine, 3, reference, 3, &report);
    expect_true("bias status", st == MS_OK);
    expect_true("bias one interval", st == MS_OK && report.interval_count == 1U);
    expect_true("bias three samples", st == MS_OK && report.intervals != NULL && report.intervals[0].sample_count == 3);
    expect_true("bias delta 120", st == MS_OK && report.counts.disagree_samples == 3U);
    ms_report_free(&report);

    MsSample gap_ref[2];
    gap_ref[0] = reference[0];
    gap_ref[1] = reference[2];
    st = compare_of(&cfg, machine, 3, gap_ref, 2, &report);
    expect_true("gap status", st == MS_OK);
    expect_true("gap splits the run", st == MS_OK && report.interval_count == 2U);
    expect_true("gap middle unverified", st == MS_OK && report.counts.unmatched_machine == 1U);
    expect_true("gap disagree ends", st == MS_OK && report.counts.disagree_samples == 2U);
    if (st == MS_OK && report.finding_count >= 2U) {
        expect_true("gap middle disposition", report.findings[1].disposition == MS_DISP_UNMATCHED_MACHINE);
    }
    ms_report_free(&report);
}

static void test_threshold_and_rate(void) {
    MsConfig cfg;
    fill_cfg(&cfg);
    MsSample machine[1];
    MsSample reference[1];
    machine[0] = sample_of(2, 0, 1030000, MS_QUALITY_OK);
    reference[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    MsReport report;
    MsStatus st = compare_of(&cfg, machine, 1, reference, 1, &report);
    expect_true("exact threshold agrees", st == MS_OK && report.counts.agree_pairs == 1U && report.interval_count == 0U);
    ms_report_free(&report);

    machine[0].milli_rpm = 1031000;
    st = compare_of(&cfg, machine, 1, reference, 1, &report);
    expect_true("one rpm over threshold disagrees", st == MS_OK && report.counts.disagree_samples == 1U);
    ms_report_free(&report);

    MsSample steady_m[2];
    MsSample steady_r[2];
    steady_m[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    steady_m[1] = sample_of(3, 1000, 1050000, MS_QUALITY_OK);
    steady_r[0] = steady_m[0];
    steady_r[1] = steady_m[1];
    st = compare_of(&cfg, steady_m, 2, steady_r, 2, &report);
    expect_true("50 rpm/s stays qualified", st == MS_OK && report.counts.dynamic_unqualified_machine == 0U);
    expect_true("50 rpm/s agrees", st == MS_OK && report.counts.agree_pairs == 2U);
    ms_report_free(&report);

    steady_m[1].milli_rpm = 1051000;
    steady_r[1].milli_rpm = 1051000;
    st = compare_of(&cfg, steady_m, 2, steady_r, 2, &report);
    expect_true("51 rpm/s is dynamic", st == MS_OK && report.counts.dynamic_unqualified_machine == 2U);
    expect_true("dynamic is not a disagreement", st == MS_OK && report.counts.disagree_samples == 0U);
    ms_report_free(&report);

    MsSample wide_m[2];
    MsSample wide_r[2];
    wide_m[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    wide_m[1] = sample_of(3, 1101, 2000000, MS_QUALITY_OK);
    wide_r[0] = wide_m[0];
    wide_r[1] = wide_m[1];
    st = compare_of(&cfg, wide_m, 2, wide_r, 2, &report);
    expect_true("gap wider than the sampling window is not a ramp",
                st == MS_OK && report.counts.dynamic_unqualified_machine == 0U && report.counts.agree_pairs == 2U);
    ms_report_free(&report);
}

static void test_matching(void) {
    MsConfig cfg;
    fill_cfg(&cfg);
    cfg.allowed_skew_ms = 50;
    MsSample machine[1];
    MsSample reference[1];
    machine[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    reference[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    const int64_t offsets[] = {0, 50, -50, 51, -51};
    const int matches[] = {1, 1, 1, 0, 0};
    for (int i = 0; i < 5; i++) {
        cfg.validated_time_offset_ms = offsets[i];
        MsReport report;
        MsStatus st = compare_of(&cfg, machine, 1, reference, 1, &report);
        int matched = st == MS_OK && report.counts.matched_pairs == 1U;
        char name[64];
        snprintf(name, sizeof name, "offset %lld", (long long)offsets[i]);
        expect_true(name, st == MS_OK && matched == matches[i]);
        ms_report_free(&report);
    }

    cfg.validated_time_offset_ms = 0;
    MsSample tie_m[1];
    MsSample tie_r[2];
    tie_m[0] = sample_of(4, 100, 1000000, MS_QUALITY_OK);
    tie_r[0] = sample_of(2, 50, 1000000, MS_QUALITY_OK);
    tie_r[1] = sample_of(3, 150, 1000000, MS_QUALITY_OK);
    MsReport report;
    MsStatus st = compare_of(&cfg, tie_m, 1, tie_r, 2, &report);
    expect_true("tie keeps the earlier reference",
                st == MS_OK && report.finding_count > 0U && report.findings[0].reference_line == 2);
    ms_report_free(&report);

    MsSample two_m[2];
    MsSample one_r[1];
    two_m[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    two_m[1] = sample_of(3, 10, 1000000, MS_QUALITY_OK);
    one_r[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    cfg.allowed_skew_ms = 100;
    st = compare_of(&cfg, two_m, 2, one_r, 1, &report);
    expect_true("one reference is used once", st == MS_OK && report.counts.matched_pairs == 1U);
    expect_true("second machine is unmatched", st == MS_OK && report.counts.unmatched_machine == 1U);
    ms_report_free(&report);
}

static void test_quality_counts(void) {
    MsConfig cfg;
    fill_cfg(&cfg);
    MsSample machine[3];
    MsSample reference[3];
    machine[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    machine[1] = sample_of(3, 1000, 1000000, MS_QUALITY_INVALID);
    machine[2] = sample_of(4, 2000, 1000000, MS_QUALITY_MISSING);
    reference[0] = sample_of(2, 0, 1000000, MS_QUALITY_OK);
    reference[1] = sample_of(3, 1000, 1000000, MS_QUALITY_INVALID);
    reference[2] = sample_of(4, 2000, 1000000, MS_QUALITY_MISSING);
    MsReport report;
    MsStatus st = compare_of(&cfg, machine, 3, reference, 3, &report);
    expect_true("quality status", st == MS_OK);
    expect_true("one agree", st == MS_OK && report.counts.agree_pairs == 1U);
    expect_true("invalid machine", st == MS_OK && report.counts.invalid_machine_rows == 1U);
    expect_true("missing machine", st == MS_OK && report.counts.missing_machine_rows == 1U);
    expect_true("invalid reference", st == MS_OK && report.counts.invalid_reference_rows == 1U);
    expect_true("missing reference", st == MS_OK && report.counts.missing_reference_rows == 1U);
    ms_report_free(&report);
}

static void test_demo_fixture(const char *root) {
    char machine_path[512];
    char reference_path[512];
    char sidecar_path[512];
    snprintf(machine_path, sizeof machine_path, "%s/fixtures/demo/machine.csv", root);
    snprintf(reference_path, sizeof reference_path, "%s/fixtures/demo/reference.csv", root);
    snprintf(sidecar_path, sizeof sidecar_path, "%s/fixtures/demo/sidecar.txt", root);
    MsError err;
    MsConfig cfg;
    MsSample *machine = NULL;
    MsSample *reference = NULL;
    size_t machine_count = 0;
    size_t reference_count = 0;
    memset(&err, 0, sizeof err);
    MsStatus st = ms_load_sidecar(sidecar_path, &cfg, &err);
    expect_true("demo sidecar", st == MS_OK);
    if (st != MS_OK) {
        fprintf(stderr, "%s\n", err.message);
        return;
    }
    st = ms_load_csv(machine_path, &machine, &machine_count, &err);
    expect_true("demo machine", st == MS_OK);
    st = ms_load_csv(reference_path, &reference, &reference_count, &err);
    expect_true("demo reference", st == MS_OK);
    if (machine == NULL || reference == NULL) {
        free(machine);
        free(reference);
        return;
    }
    MsReport report;
    st = compare_of(&cfg, machine, machine_count, reference, reference_count, &report);
    expect_true("demo compare", st == MS_OK);
    expect_true("demo coverage numerator", st == MS_OK && report.counts.valid_steady_pairs == 8U);
    expect_true("demo coverage denominator", st == MS_OK && report.counts.ok_machine_rows == 13U);
    expect_true("demo intervals", st == MS_OK && report.interval_count == 3U);
    if (st == MS_OK && report.interval_count == 3U) {
        expect_true("bias window", report.intervals[0].sample_count == 3 && report.intervals[0].start_machine_line == 6 &&
                                       report.intervals[0].end_machine_line == 8 &&
                                       report.intervals[0].start_reference_line == 5 &&
                                       report.intervals[0].end_reference_line == 7);
        expect_true("gap start", report.intervals[1].sample_count == 1 && report.intervals[1].start_machine_line == 13);
        expect_true("gap end", report.intervals[2].sample_count == 1 && report.intervals[2].start_machine_line == 15);
    }
    expect_true("demo invalid", st == MS_OK && report.counts.invalid_machine_rows == 1U);
    expect_true("demo missing", st == MS_OK && report.counts.missing_machine_rows == 1U);
    expect_true("demo dynamic", st == MS_OK && report.counts.dynamic_unqualified_machine == 4U);
    expect_true("demo unmatched machine", st == MS_OK && report.counts.unmatched_machine == 1U);
    expect_true("demo unmatched reference", st == MS_OK && report.counts.unmatched_reference == 1U);
    char *summary = NULL;
    memset(&err, 0, sizeof err);
    st = ms_render_summary(&cfg, &report, &summary, &err);
    expect_true("demo summary", st == MS_OK && summary != NULL);
    if (summary != NULL) {
        expect_true("summary coverage", strstr(summary, "valid_pair_coverage=8/13") != NULL);
        expect_true("summary limitation", strstr(summary, "safe-to-operate") != NULL);
        expect_true("summary unverified gap", strstr(summary, "reason=unmatched_machine elapsed_ms=17000") != NULL);
        expect_true("summary ramp", strstr(summary, "reason=dynamic_unqualified") != NULL);
        free(summary);
    }
    ms_report_free(&report);
    free(machine);
    free(reference);
}

static void test_performance(void) {
    const size_t count = 10000U;
    MsSample *machine = calloc(count, sizeof(*machine));
    MsSample *reference = calloc(count, sizeof(*reference));
    expect_true("perf allocation", machine != NULL && reference != NULL);
    if (machine == NULL || reference == NULL) {
        free(machine);
        free(reference);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        machine[i] = sample_of((int64_t)i + 2, (int64_t)i, 1000000, MS_QUALITY_OK);
        reference[i] = machine[i];
    }
    MsConfig cfg;
    fill_cfg(&cfg);
    cfg.allowed_skew_ms = 0;
    MsReport report;
    clock_t start = clock();
    MsStatus st = compare_of(&cfg, machine, count, reference, count, &report);
    clock_t stop = clock();
    double seconds = (double)(stop - start) / (double)CLOCKS_PER_SEC;
    printf("perf_10000_seconds=%.6f\n", seconds);
    expect_true("perf status", st == MS_OK && report.counts.agree_pairs == count);
    expect_true("perf budget 1s", seconds < 1.0);
    ms_report_free(&report);
    free(machine);
    free(reference);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: test_compare ROOT\n");
        return 2;
    }
    test_bias_and_gap();
    test_threshold_and_rate();
    test_matching();
    test_quality_counts();
    test_demo_fixture(argv[1]);
    test_performance();
    if (g_failed != 0) {
        fprintf(stderr, "%d compare checks failed\n", g_failed);
        return 1;
    }
    puts("compare checks passed");
    return 0;
}
