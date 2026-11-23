#include "machinesight/ms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void expect_status(const char *name, MsStatus got, MsStatus want, const MsError *err) {
    if (got != want) {
        fprintf(stderr, "FAIL %s status %d (%s) message: %s\n", name, (int)got, ms_status_name(got),
                err->message);
        g_failed++;
    }
}

static void expect_true(const char *name, int cond) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", name);
        g_failed++;
    }
}

static const char *DEMO_SIDE =
    "schema_version=1\n"
    "sampling_interval_ms=1000\n"
    "machine_source_id=legacy-bench-demo\n"
    "reference_source_id=ref-tach-demo\n"
    "validated_time_offset_ms=0\n"
    "allowed_skew_ms=100\n"
    "reference_uncertainty_rpm=5\n"
    "comparison_tolerance_rpm=25\n"
    "max_reference_rate_rpm_per_s=50\n"
    "clock_alignment_provenance=fixture-declared-zero-offset\n"
    "calibration_status=synthetic-teaching-fixture\n"
    "time_base=elapsed_ms\n";

static void test_valid_and_edges(void) {
    MsError err;
    MsSample *samples = NULL;
    size_t count = 0;
    const char *text = "elapsed_ms,rpm,quality\r\n0,1000.5,ok\n1000,1000.125,ok";
    memset(&err, 0, sizeof err);
    MsStatus st = ms_parse_csv(text, strlen(text), "mem.csv", &samples, &count, &err);
    expect_status("crlf and final line", st, MS_OK, &err);
    expect_true("two rows", st == MS_OK && count == 2U);
    if (st == MS_OK) {
        expect_true("milli 1000.5", samples[0].milli_rpm == 1000500);
        expect_true("milli 1000.125", samples[1].milli_rpm == 1000125);
        expect_true("line numbers", samples[0].line == 2 && samples[1].line == 3);
    }
    free(samples);

    const char *bad_rpm = "elapsed_ms,rpm,quality\n0,fast,ok\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(bad_rpm, strlen(bad_rpm), "bad.csv", &samples, &count, &err);
    expect_status("malformed rpm", st, MS_ERR_INPUT, &err);
    expect_true("rpm message", strstr(err.message, "rpm") != NULL);

    const char *dup = "elapsed_ms,rpm,quality\n0,1.000,ok\n0,1.000,ok\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(dup, strlen(dup), "dup.csv", &samples, &count, &err);
    expect_status("duplicate", st, MS_ERR_INPUT, &err);
    expect_true("duplicate message", strstr(err.message, "duplicate") != NULL);

    const char *overflow = "elapsed_ms,rpm,quality\n999999999999999999999,1000.000,ok\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(overflow, strlen(overflow), "overflow.csv", &samples, &count, &err);
    expect_status("overflow", st, MS_ERR_INPUT, &err);
    expect_true("overflow message", strstr(err.message, "overflow") != NULL);

    const char *back = "elapsed_ms,rpm,quality\n0,1,ok\n2000,1,ok\n1000,1,ok\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(back, strlen(back), "order.csv", &samples, &count, &err);
    expect_status("decreasing", st, MS_ERR_INPUT, &err);

    const char *qual = "elapsed_ms,rpm,quality\n0,1,bad\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(qual, strlen(qual), "qual.csv", &samples, &count, &err);
    expect_status("quality", st, MS_ERR_INPUT, &err);

    const char *cols = "elapsed_ms,rpm,quality\n0,1,ok,extra\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(cols, strlen(cols), "cols.csv", &samples, &count, &err);
    expect_status("columns", st, MS_ERR_INPUT, &err);

    char raw[64];
    memcpy(raw, "elapsed_ms,rpm,quality\n0,1.000,ok\n", 32);
    raw[28] = '\0';
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(raw, 32, "nul.csv", &samples, &count, &err);
    expect_status("nul", st, MS_ERR_INPUT, &err);
    expect_true("nul message", strstr(err.message, "NUL") != NULL);

    char long_line[800];
    size_t pos = 0;
    const char *hdr = "elapsed_ms,rpm,quality\n";
    memcpy(long_line, hdr, strlen(hdr));
    pos = strlen(hdr);
    memset(long_line + pos, 'a', 520);
    pos += 520;
    long_line[pos++] = '\n';
    memset(&err, 0, sizeof err);
    st = ms_parse_csv(long_line, pos, "long.csv", &samples, &count, &err);
    expect_status("long line", st, MS_ERR_LIMIT, &err);
    expect_true("long message", strstr(err.message, "511") != NULL);
}

static void test_row_limit(void) {
    size_t cap = 64U + 10001U * 32U;
    char *text = malloc(cap);
    expect_true("row limit allocation", text != NULL);
    if (text == NULL) {
        return;
    }
    size_t pos = 0;
    const char *hdr = "elapsed_ms,rpm,quality\n";
    memcpy(text, hdr, strlen(hdr));
    pos = strlen(hdr);
    for (int i = 0; i <= MS_MAX_ROWS; i++) {
        int wrote = snprintf(text + pos, cap - pos, "%d,1,ok\n", i);
        expect_true("row limit write", wrote > 0 && (size_t)wrote < cap - pos);
        if (wrote < 0 || (size_t)wrote >= cap - pos) {
            free(text);
            return;
        }
        pos += (size_t)wrote;
    }
    MsError err;
    MsSample *samples = NULL;
    size_t count = 0;
    memset(&err, 0, sizeof err);
    MsStatus st = ms_parse_csv(text, pos, "rows.csv", &samples, &count, &err);
    expect_status("row limit", st, MS_ERR_LIMIT, &err);
    expect_true("row limit message", strstr(err.message, "10000") != NULL);
    free(samples);
    free(text);
}

static void test_sidecar(void) {
    MsError err;
    MsConfig cfg;
    memset(&err, 0, sizeof err);
    MsStatus st = ms_parse_sidecar(DEMO_SIDE, strlen(DEMO_SIDE), "side.txt", &cfg, &err);
    expect_status("sidecar", st, MS_OK, &err);
    if (st == MS_OK) {
        expect_true("tolerance milli", cfg.comparison_tolerance_milli_rpm == 25000);
        expect_true("uncertainty milli", cfg.reference_uncertainty_milli_rpm == 5000);
        expect_true("rate milli", cfg.max_reference_rate_milli_rpm_per_s == 50000);
        expect_true("skew", cfg.allowed_skew_ms == 100);
    }

    const char *uncal =
        "schema_version=1\n"
        "sampling_interval_ms=1000\n"
        "machine_source_id=machine\n"
        "reference_source_id=reference\n"
        "validated_time_offset_ms=0\n"
        "allowed_skew_ms=100\n"
        "reference_uncertainty_rpm=5\n"
        "comparison_tolerance_rpm=25\n"
        "max_reference_rate_rpm_per_s=50\n"
        "clock_alignment_provenance=fixture-declared-zero-offset\n"
        "calibration_status=uncalibrated\n"
        "time_base=elapsed_ms\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_sidecar(uncal, strlen(uncal), "uncal.txt", &cfg, &err);
    expect_status("uncalibrated", st, MS_ERR_UNSUPPORTED, &err);
    expect_true("calibration message", strstr(err.message, "calibration_status") != NULL);

    const char *wall = "schema_version=1\n"
                       "sampling_interval_ms=1000\n"
                       "machine_source_id=machine\n"
                       "reference_source_id=reference\n"
                       "validated_time_offset_ms=0\n"
                       "allowed_skew_ms=100\n"
                       "reference_uncertainty_rpm=5\n"
                       "comparison_tolerance_rpm=25\n"
                       "max_reference_rate_rpm_per_s=50\n"
                       "clock_alignment_provenance=fixture-declared-zero-offset\n"
                       "calibration_status=reference-stated\n"
                       "time_base=unix_ms\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_sidecar(wall, strlen(wall), "wall.txt", &cfg, &err);
    expect_status("time base", st, MS_ERR_UNSUPPORTED, &err);

    const char *missing = "schema_version=1\n";
    memset(&err, 0, sizeof err);
    st = ms_parse_sidecar(missing, strlen(missing), "missing.txt", &cfg, &err);
    expect_status("missing key", st, MS_ERR_INPUT, &err);
    expect_true("missing key name", strstr(err.message, "sampling_interval_ms") != NULL);
}

int main(void) {
    test_valid_and_edges();
    test_row_limit();
    test_sidecar();
    if (g_failed != 0) {
        fprintf(stderr, "%d parse checks failed\n", g_failed);
        return 1;
    }
    puts("parse checks passed");
    return 0;
}
