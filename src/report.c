#include "machinesight/ms.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ms_status_name(MsStatus status) {
    switch (status) {
    case MS_OK:
        return "ok";
    case MS_ERR_USAGE:
        return "usage";
    case MS_ERR_INPUT:
        return "input";
    case MS_ERR_IO:
        return "io";
    case MS_ERR_LIMIT:
        return "limit";
    case MS_ERR_UNSUPPORTED:
        return "unsupported";
    case MS_ERR_INTERNAL:
        return "internal";
    default:
        return "unknown";
    }
}

void ms_report_free(MsReport *report) {
    if (report == NULL) {
        return;
    }
    free(report->findings);
    free(report->intervals);
    report->findings = NULL;
    report->intervals = NULL;
    report->finding_count = 0;
    report->interval_count = 0;
}

typedef struct MsBuf {
    char *data;
    size_t len;
    size_t cap;
} MsBuf;

static int buf_reserve(MsBuf *buf, size_t extra) {
    if (extra > SIZE_MAX - buf->len - 1U) {
        return -1;
    }
    size_t need = buf->len + extra + 1U;
    if (need <= buf->cap) {
        return 0;
    }
    size_t cap = buf->cap == 0U ? 256U : buf->cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2U) {
            return -1;
        }
        cap *= 2U;
    }
    char *grown = realloc(buf->data, cap);
    if (grown == NULL) {
        return -1;
    }
    buf->data = grown;
    buf->cap = cap;
    return 0;
}

static int buf_append(MsBuf *buf, const char *text, size_t n) {
    if (buf_reserve(buf, n) != 0) {
        return -1;
    }
    memcpy(buf->data + buf->len, text, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return 0;
}

static int buf_puts(MsBuf *buf, const char *text) {
    return buf_append(buf, text, strlen(text));
}

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static int buf_printf(MsBuf *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int stored = -1;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int needed = vsnprintf(NULL, 0, fmt, ap);
    if (needed >= 0 && buf_reserve(buf, (size_t)needed) == 0) {
        vsnprintf(buf->data + buf->len, (size_t)needed + 1U, fmt, copy);
        buf->len += (size_t)needed;
        stored = 0;
    }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    va_end(copy);
    va_end(ap);
    return stored;
}

static void format_rpm(char *dst, size_t n, int64_t milli) {
    int64_t value = milli;
    const char *sign = "";
    if (value < 0) {
        sign = "-";
        value = -value;
    }
    int64_t whole = value / 1000;
    int64_t frac = value % 1000;
    snprintf(dst, n, "%s%" PRId64 ".%03" PRId64, sign, whole, frac);
}

static const char *disposition_name(MsDisposition disposition) {
    switch (disposition) {
    case MS_DISP_AGREE:
        return "agree";
    case MS_DISP_DISAGREE:
        return "disagree";
    case MS_DISP_DYNAMIC:
        return "dynamic_unqualified";
    case MS_DISP_UNMATCHED_MACHINE:
        return "unmatched_machine";
    case MS_DISP_UNMATCHED_REFERENCE:
        return "unmatched_reference";
    case MS_DISP_INVALID_MACHINE:
        return "invalid_machine";
    case MS_DISP_INVALID_REFERENCE:
        return "invalid_reference";
    case MS_DISP_MISSING_MACHINE:
        return "missing_machine";
    case MS_DISP_MISSING_REFERENCE:
        return "missing_reference";
    default:
        return "unknown";
    }
}

static int finish_text(MsBuf *buf, char **text, MsError *err) {
    if (buf->data == NULL) {
        char *empty = malloc(1U);
        if (empty == NULL) {
            ms_set_error(err, NULL, 0, "out of memory while rendering a report");
            return -1;
        }
        empty[0] = '\0';
        *text = empty;
        return 0;
    }
    *text = buf->data;
    buf->data = NULL;
    return 0;
}

static void fail_buf(MsBuf *buf, char **text, MsError *err) {
    free(buf->data);
    buf->data = NULL;
    *text = NULL;
    ms_set_error(err, NULL, 0, "out of memory while rendering a report");
}

MsStatus ms_render_summary(const MsConfig *cfg, const MsReport *report, char **text, MsError *err) {
    MsBuf buf;
    char rpm[64];
    memset(&buf, 0, sizeof buf);
    if (text == NULL || cfg == NULL || report == NULL) {
        ms_set_error(err, NULL, 0, "internal render call is missing a report");
        return MS_ERR_INTERNAL;
    }
    *text = NULL;

    int failed = 0;
    failed |= buf_puts(&buf, "MachineSight RPM comparison\n");
    failed |= buf_printf(&buf, "machinesight_version=%s\n", MS_VERSION);
    failed |= buf_puts(&buf, "report_schema=1\n");
    failed |= buf_printf(&buf, "schema_version=%" PRId32 "\n", cfg->schema_version);
    failed |= buf_printf(&buf, "calibration_status=%s\n", cfg->calibration_status);
    failed |= buf_printf(&buf, "time_base=%s\n", cfg->time_base);
    failed |= buf_printf(&buf, "clock_alignment_provenance=%s\n", cfg->clock_alignment_provenance);
    failed |= buf_printf(&buf, "machine_source_id=%s\n", cfg->machine_source_id);
    failed |= buf_printf(&buf, "reference_source_id=%s\n", cfg->reference_source_id);
    failed |= buf_puts(&buf, "units=elapsed_milliseconds,revolutions_per_minute\n");
    format_rpm(rpm, sizeof rpm, cfg->comparison_tolerance_milli_rpm);
    failed |= buf_printf(&buf, "comparison_tolerance_rpm=%s\n", rpm);
    format_rpm(rpm, sizeof rpm, cfg->reference_uncertainty_milli_rpm);
    failed |= buf_printf(&buf, "reference_uncertainty_rpm=%s\n", rpm);
    format_rpm(rpm, sizeof rpm, report->threshold_milli_rpm);
    failed |= buf_printf(&buf, "threshold_rpm=%s\n", rpm);
    failed |= buf_printf(&buf, "allowed_skew_ms=%" PRId64 "\n", cfg->allowed_skew_ms);
    failed |= buf_printf(&buf, "validated_time_offset_ms=%" PRId64 "\n", cfg->validated_time_offset_ms);
    failed |= buf_printf(&buf, "sampling_interval_ms=%" PRId64 "\n", cfg->sampling_interval_ms);
    format_rpm(rpm, sizeof rpm, cfg->max_reference_rate_milli_rpm_per_s);
    failed |= buf_printf(&buf, "max_reference_rate_rpm_per_s=%s\n", rpm);
    failed |= buf_puts(&buf,
                       "tie_resolution=nearest_unused_reference_equidistant_earlier_elapsed_then_lower_"
                       "line_machine_time_order\n");
    failed |= buf_puts(&buf,
                       "steady_rule=adjacent_reference_samples_inside_the_sampling_window_above_max_rate_"
                       "are_unqualified\n");
    failed |= buf_puts(&buf,
                       "limitation=Comparable steady-state disagreements only. Disagreement is not an "
                       "equipment condition, a safe-to-operate decision, or a shutdown instruction. "
                       "Missing, dynamic, and unsynchronized spans stay unverified.\n");
    failed |= buf_printf(&buf, "ok_machine_rows=%zu\n", report->counts.ok_machine_rows);
    failed |= buf_printf(&buf, "ok_reference_rows=%zu\n", report->counts.ok_reference_rows);
    failed |= buf_printf(&buf, "invalid_machine_rows=%zu\n", report->counts.invalid_machine_rows);
    failed |= buf_printf(&buf, "invalid_reference_rows=%zu\n", report->counts.invalid_reference_rows);
    failed |= buf_printf(&buf, "missing_machine_rows=%zu\n", report->counts.missing_machine_rows);
    failed |= buf_printf(&buf, "missing_reference_rows=%zu\n", report->counts.missing_reference_rows);
    failed |= buf_printf(&buf, "matched_pairs=%zu\n", report->counts.matched_pairs);
    failed |= buf_printf(&buf, "valid_steady_pairs=%zu\n", report->counts.valid_steady_pairs);
    failed |= buf_printf(&buf, "agree_pairs=%zu\n", report->counts.agree_pairs);
    failed |= buf_printf(&buf, "disagree_samples=%zu\n", report->counts.disagree_samples);
    failed |= buf_printf(&buf, "dynamic_unqualified_machine=%zu\n", report->counts.dynamic_unqualified_machine);
    failed |= buf_printf(&buf, "unmatched_machine=%zu\n", report->counts.unmatched_machine);
    failed |= buf_printf(&buf, "unmatched_reference=%zu\n", report->counts.unmatched_reference);
    if (report->counts.ok_machine_rows == 0U) {
        failed |= buf_puts(&buf, "valid_pair_coverage=0/0\n");
        failed |= buf_puts(&buf, "valid_pair_coverage_decimal=0.0000\n");
    } else {
        unsigned long long den = (unsigned long long)report->counts.ok_machine_rows;
        unsigned long long num = (unsigned long long)report->counts.valid_steady_pairs;
        unsigned long long scaled = num * 10000ULL / den;
        failed |= buf_printf(&buf, "valid_pair_coverage=%zu/%zu\n", report->counts.valid_steady_pairs,
                             report->counts.ok_machine_rows);
        failed |= buf_printf(&buf, "valid_pair_coverage_decimal=%llu.%04llu\n", scaled / 10000ULL, scaled % 10000ULL);
    }
    failed |= buf_printf(&buf, "disagreement_intervals=%zu\n", report->counts.disagreement_intervals);

    for (size_t i = 0; i < report->interval_count; i++) {
        const MsInterval *interval = &report->intervals[i];
        char min_rpm[64];
        char max_rpm[64];
        format_rpm(min_rpm, sizeof min_rpm, interval->min_abs_delta_milli);
        format_rpm(max_rpm, sizeof max_rpm, interval->max_abs_delta_milli);
        failed |= buf_printf(&buf,
                             "interval=%zu samples=%" PRId64 " machine_lines=%" PRId64 "-%" PRId64
                             " reference_lines=%" PRId64 "-%" PRId64 " elapsed_ms=%" PRId64 "-%" PRId64
                             " min_abs_delta_rpm=%s max_abs_delta_rpm=%s\n",
                             i + 1U, interval->sample_count, interval->start_machine_line,
                             interval->end_machine_line, interval->start_reference_line,
                             interval->end_reference_line, interval->start_elapsed_ms, interval->end_elapsed_ms,
                             min_rpm, max_rpm);
    }

    for (size_t i = 0; i < report->finding_count; i++) {
        const MsFinding *finding = &report->findings[i];
        if (finding->disposition == MS_DISP_AGREE || finding->disposition == MS_DISP_DISAGREE) {
            continue;
        }
        char machine_line[32];
        char reference_line[32];
        machine_line[0] = '\0';
        reference_line[0] = '\0';
        if (finding->has_machine) {
            snprintf(machine_line, sizeof machine_line, "%" PRId64, finding->machine_line);
        }
        if (finding->has_reference) {
            snprintf(reference_line, sizeof reference_line, "%" PRId64, finding->reference_line);
        }
        int64_t elapsed = finding->has_machine ? finding->machine_elapsed_ms : finding->reference_elapsed_ms;
        failed |= buf_printf(&buf, "unverified machine_line=%s reference_line=%s reason=%s elapsed_ms=%" PRId64 "\n",
                             machine_line, reference_line, disposition_name(finding->disposition), elapsed);
    }

    if (failed) {
        fail_buf(&buf, text, err);
        return MS_ERR_INTERNAL;
    }
    if (finish_text(&buf, text, err) != 0) {
        return MS_ERR_INTERNAL;
    }
    return MS_OK;
}

static int append_optional_i64(MsBuf *buf, int present, int64_t value) {
    if (!present) {
        return buf_puts(buf, ",");
    }
    return buf_printf(buf, "%" PRId64 ",", value);
}

static int append_optional_rpm(MsBuf *buf, int present, int64_t milli) {
    if (!present) {
        return buf_puts(buf, ",");
    }
    char text[64];
    format_rpm(text, sizeof text, milli);
    return buf_printf(buf, "%s,", text);
}

MsStatus ms_render_findings(const MsReport *report, char **text, MsError *err) {
    MsBuf buf;
    memset(&buf, 0, sizeof buf);
    if (text == NULL || report == NULL) {
        ms_set_error(err, NULL, 0, "internal render call is missing a report");
        return MS_ERR_INTERNAL;
    }
    *text = NULL;
    int failed = buf_puts(&buf,
                          "disposition,machine_line,reference_line,machine_elapsed_ms,reference_elapsed_ms,"
                          "aligned_machine_ms,offset_ms,machine_rpm,reference_rpm,abs_delta_rpm,threshold_rpm\n");
    for (size_t i = 0; i < report->finding_count; i++) {
        const MsFinding *finding = &report->findings[i];
        failed |= buf_printf(&buf, "%s,", disposition_name(finding->disposition));
        failed |= append_optional_i64(&buf, finding->has_machine, finding->machine_line);
        failed |= append_optional_i64(&buf, finding->has_reference, finding->reference_line);
        failed |= append_optional_i64(&buf, finding->has_machine, finding->machine_elapsed_ms);
        failed |= append_optional_i64(&buf, finding->has_reference, finding->reference_elapsed_ms);
        failed |= append_optional_i64(&buf, finding->has_alignment, finding->aligned_machine_ms);
        failed |= append_optional_i64(&buf, finding->has_alignment && finding->has_reference, finding->offset_ms);
        failed |= append_optional_rpm(&buf, finding->has_machine, finding->machine_milli_rpm);
        failed |= append_optional_rpm(&buf, finding->has_reference, finding->reference_milli_rpm);
        failed |= append_optional_rpm(&buf, finding->has_delta, finding->abs_delta_milli);
        if (finding->has_delta) {
            char rpm[64];
            format_rpm(rpm, sizeof rpm, finding->threshold_milli);
            failed |= buf_printf(&buf, "%s\n", rpm);
        } else {
            failed |= buf_puts(&buf, "\n");
        }
    }
    if (failed) {
        fail_buf(&buf, text, err);
        return MS_ERR_INTERNAL;
    }
    if (finish_text(&buf, text, err) != 0) {
        return MS_ERR_INTERNAL;
    }
    return MS_OK;
}

MsStatus ms_render_intervals(const MsReport *report, char **text, MsError *err) {
    MsBuf buf;
    memset(&buf, 0, sizeof buf);
    if (text == NULL || report == NULL) {
        ms_set_error(err, NULL, 0, "internal render call is missing a report");
        return MS_ERR_INTERNAL;
    }
    *text = NULL;
    int failed = buf_puts(&buf,
                          "interval_id,samples,start_machine_line,end_machine_line,start_reference_line,"
                          "end_reference_line,start_elapsed_ms,end_elapsed_ms,min_abs_delta_rpm,"
                          "max_abs_delta_rpm\n");
    for (size_t i = 0; i < report->interval_count; i++) {
        const MsInterval *interval = &report->intervals[i];
        char min_rpm[64];
        char max_rpm[64];
        format_rpm(min_rpm, sizeof min_rpm, interval->min_abs_delta_milli);
        format_rpm(max_rpm, sizeof max_rpm, interval->max_abs_delta_milli);
        failed |= buf_printf(&buf,
                             "%zu,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
                             ",%s,%s\n",
                             i + 1U, interval->sample_count, interval->start_machine_line, interval->end_machine_line,
                             interval->start_reference_line, interval->end_reference_line, interval->start_elapsed_ms,
                             interval->end_elapsed_ms, min_rpm, max_rpm);
    }
    if (failed) {
        fail_buf(&buf, text, err);
        return MS_ERR_INTERNAL;
    }
    if (finish_text(&buf, text, err) != 0) {
        return MS_ERR_INTERNAL;
    }
    return MS_OK;
}
