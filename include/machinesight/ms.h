#ifndef MACHINESIGHT_MS_H
#define MACHINESIGHT_MS_H

#include <stddef.h>
#include <stdint.h>

#define MS_VERSION "0.1.0"
#define MS_MAX_LINE_CHARS 511
#define MS_MAX_FIELD_CHARS 63
#define MS_MAX_ROWS 10000
#define MS_MAX_FILE_BYTES (8 * 1024 * 1024)
#define MS_MAX_ELAPSED_MS 604800000LL
#define MS_MAX_RPM_MILLI 200000000LL
#define MS_MAX_OFFSET_MS 86400000LL
#define MS_MAX_SKEW_MS 86400000LL
#define MS_MAX_SAMPLING_MS 86400000LL
#define MS_MAX_RATE_MILLI_PER_S 1000000000LL
#define MS_TEXT_CAP 96

typedef enum MsStatus {
    MS_OK = 0,
    MS_ERR_USAGE = 1,
    MS_ERR_INPUT = 2,
    MS_ERR_IO = 3,
    MS_ERR_LIMIT = 4,
    MS_ERR_UNSUPPORTED = 5,
    MS_ERR_INTERNAL = 6
} MsStatus;

typedef enum MsQuality {
    MS_QUALITY_OK = 0,
    MS_QUALITY_INVALID = 1,
    MS_QUALITY_MISSING = 2
} MsQuality;

typedef enum MsDisposition {
    MS_DISP_AGREE = 0,
    MS_DISP_DISAGREE = 1,
    MS_DISP_DYNAMIC = 2,
    MS_DISP_UNMATCHED_MACHINE = 3,
    MS_DISP_UNMATCHED_REFERENCE = 4,
    MS_DISP_INVALID_MACHINE = 5,
    MS_DISP_INVALID_REFERENCE = 6,
    MS_DISP_MISSING_MACHINE = 7,
    MS_DISP_MISSING_REFERENCE = 8
} MsDisposition;

typedef struct MsError {
    char message[1024];
    int64_t line;
} MsError;

typedef struct MsSample {
    int64_t line;
    int64_t elapsed_ms;
    int64_t milli_rpm;
    MsQuality quality;
} MsSample;

typedef struct MsConfig {
    int32_t schema_version;
    int64_t sampling_interval_ms;
    int64_t validated_time_offset_ms;
    int64_t allowed_skew_ms;
    int64_t reference_uncertainty_milli_rpm;
    int64_t comparison_tolerance_milli_rpm;
    int64_t max_reference_rate_milli_rpm_per_s;
    char machine_source_id[MS_TEXT_CAP];
    char reference_source_id[MS_TEXT_CAP];
    char clock_alignment_provenance[MS_TEXT_CAP];
    char calibration_status[MS_TEXT_CAP];
    char time_base[MS_TEXT_CAP];
} MsConfig;

typedef struct MsFinding {
    MsDisposition disposition;
    int has_machine;
    int has_reference;
    int has_alignment;
    int has_delta;
    int64_t machine_line;
    int64_t reference_line;
    int64_t machine_elapsed_ms;
    int64_t reference_elapsed_ms;
    int64_t aligned_machine_ms;
    int64_t offset_ms;
    int64_t machine_milli_rpm;
    int64_t reference_milli_rpm;
    int64_t abs_delta_milli;
    int64_t threshold_milli;
} MsFinding;

typedef struct MsInterval {
    int64_t sample_count;
    int64_t start_machine_line;
    int64_t end_machine_line;
    int64_t start_reference_line;
    int64_t end_reference_line;
    int64_t start_elapsed_ms;
    int64_t end_elapsed_ms;
    int64_t min_abs_delta_milli;
    int64_t max_abs_delta_milli;
} MsInterval;

typedef struct MsCounts {
    size_t machine_rows;
    size_t reference_rows;
    size_t ok_machine_rows;
    size_t ok_reference_rows;
    size_t invalid_machine_rows;
    size_t invalid_reference_rows;
    size_t missing_machine_rows;
    size_t missing_reference_rows;
    size_t matched_pairs;
    size_t valid_steady_pairs;
    size_t agree_pairs;
    size_t disagree_samples;
    size_t dynamic_unqualified_machine;
    size_t unmatched_machine;
    size_t unmatched_reference;
    size_t disagreement_intervals;
} MsCounts;

typedef struct MsReport {
    MsFinding *findings;
    size_t finding_count;
    MsInterval *intervals;
    size_t interval_count;
    MsCounts counts;
    int64_t threshold_milli_rpm;
} MsReport;

void ms_set_error(MsError *err, const char *path, int64_t line, const char *fmt, ...);
const char *ms_status_name(MsStatus status);
void ms_report_free(MsReport *report);

MsStatus ms_validate_config(const MsConfig *cfg, MsError *err);
MsStatus ms_parse_sidecar(const char *data, size_t length, const char *name, MsConfig *cfg,
                          MsError *err);
MsStatus ms_parse_csv(const char *data, size_t length, const char *name, MsSample **samples,
                      size_t *count, MsError *err);
MsStatus ms_load_sidecar(const char *path, MsConfig *cfg, MsError *err);
MsStatus ms_load_csv(const char *path, MsSample **samples, size_t *count, MsError *err);

/* Pair ok machine samples to unused ok reference samples. On failure the report is empty. */
MsStatus ms_compare(const MsConfig *cfg, const MsSample *machine, size_t machine_count,
                    const MsSample *reference, size_t reference_count, MsReport *report,
                    MsError *err);

MsStatus ms_render_summary(const MsConfig *cfg, const MsReport *report, char **text, MsError *err);
MsStatus ms_render_findings(const MsReport *report, char **text, MsError *err);
MsStatus ms_render_intervals(const MsReport *report, char **text, MsError *err);

#endif
