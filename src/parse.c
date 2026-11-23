#include "machinesight/ms.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
__attribute__((format(printf, 4, 5)))
#endif
void ms_set_error(MsError *err, const char *path, int64_t line, const char *fmt, ...) {
    char detail[640];
    va_list ap;

    if (err == NULL) {
        return;
    }
    err->line = line;
    va_start(ap, fmt);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int wrote = vsnprintf(detail, sizeof detail, fmt, ap);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    va_end(ap);
    if (wrote < 0) {
        snprintf(err->message, sizeof err->message, "internal error while formatting a message");
        return;
    }

    char path_part[181];
    path_part[0] = '\0';
    if (path != NULL && path[0] != '\0') {
        size_t n = strlen(path);
        if (n > sizeof path_part - 1U) {
            n = sizeof path_part - 1U;
        }
        memcpy(path_part, path, n);
        path_part[n] = '\0';
    }

    size_t pos = 0;
    if (path_part[0] != '\0') {
        size_t n = strlen(path_part);
        memcpy(err->message + pos, path_part, n);
        pos += n;
        err->message[pos++] = ':';
        if (line > 0) {
            char num[32];
            int nn = snprintf(num, sizeof num, "%" PRId64, line);
            if (nn > 0 && (size_t)nn < sizeof num) {
                memcpy(err->message + pos, num, (size_t)nn);
                pos += (size_t)nn;
            }
            err->message[pos++] = ':';
        }
        err->message[pos++] = ' ';
    }
    size_t dlen = strlen(detail);
    if (dlen > sizeof err->message - pos - 1U) {
        dlen = sizeof err->message - pos - 1U;
    }
    memcpy(err->message + pos, detail, dlen);
    pos += dlen;
    err->message[pos] = '\0';
}

static void trim_span(const char **start, size_t *len) {
    const char *s = *start;
    size_t n = *len;
    while (n > 0U && (s[0] == ' ' || s[0] == '\t')) {
        s++;
        n--;
    }
    while (n > 0U && (s[n - 1U] == ' ' || s[n - 1U] == '\t')) {
        n--;
    }
    *start = s;
    *len = n;
}

static MsStatus next_line(const char *data, size_t length, size_t *index, int64_t *line_no,
                          const char *name, const char **ptr, size_t *len, int *got, MsError *err) {
    *got = 0;
    if (*index >= length) {
        return MS_OK;
    }

    size_t start = *index;
    int saw_nul = 0;
    int too_long = 0;
    int bad_char = 0;
    while (*index < length && data[*index] != '\n') {
        unsigned char c = (unsigned char)data[*index];
        if (c == 0U) {
            saw_nul = 1;
        } else if (c != '\t' && c != '\r' && (c < 32U || c > 126U)) {
            bad_char = 1;
        }
        if ((*index - start) >= (size_t)MS_MAX_LINE_CHARS) {
            too_long = 1;
        }
        *index += 1U;
    }
    size_t raw = *index - start;
    if (*index < length && data[*index] == '\n') {
        *index += 1U;
    }
    if (raw > 0U && data[start + raw - 1U] == '\r') {
        raw -= 1U;
    }
    int64_t current = *line_no;
    *line_no += 1;

    if (saw_nul) {
        ms_set_error(err, name, current, "embedded NUL byte; remove the NUL and keep the row as text");
        return MS_ERR_INPUT;
    }
    if (too_long) {
        ms_set_error(err, name, current, "line exceeds %d characters", MS_MAX_LINE_CHARS);
        return MS_ERR_LIMIT;
    }
    if (bad_char) {
        ms_set_error(err, name, current, "line contains a character outside printable ASCII");
        return MS_ERR_INPUT;
    }
    for (size_t k = 0; k < raw; k++) {
        if (data[start + k] == '\r') {
            ms_set_error(err, name, current, "carriage return is only allowed at the end of a line");
            return MS_ERR_INPUT;
        }
    }
    *ptr = data + start;
    *len = raw;
    *got = 1;
    return MS_OK;
}

static int parse_i64(const char *text, int64_t min_v, int64_t max_v, int64_t *out, const char **reason) {
    size_t i = 0;
    int neg = 0;
    if (text[0] == '\0') {
        *reason = "value is empty";
        return -1;
    }
    if (text[0] == '-') {
        neg = 1;
        i = 1;
    }
    if (text[i] == '\0' || !isdigit((unsigned char)text[i])) {
        *reason = "value is not an integer";
        return -1;
    }
    int64_t value = 0;
    for (; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        if (!isdigit(c)) {
            *reason = "value is not an integer";
            return -1;
        }
        int digit = (int)(c - (unsigned char)'0');
        if (value > (INT64_MAX - digit) / 10) {
            *reason = "integer overflows the supported range";
            return -1;
        }
        value = value * 10 + digit;
    }
    if (neg) {
        value = -value;
    }
    if (value < min_v || value > max_v) {
        *reason = "integer is outside the supported range";
        return -1;
    }
    *out = value;
    return 0;
}

static int parse_rpm_milli(const char *text, int64_t *milli, const char **reason) {
    if (text[0] == '\0') {
        *reason = "rpm is empty";
        return -1;
    }
    if (text[0] == '-' || text[0] == '+') {
        *reason = "rpm must be a non-negative decimal with at most 3 fractional digits";
        return -1;
    }
    int64_t whole = 0;
    int64_t frac = 0;
    int seen_digit = 0;
    int seen_dot = 0;
    int frac_digits = 0;
    for (size_t i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '.') {
            if (seen_dot) {
                *reason = "rpm has more than one decimal point";
                return -1;
            }
            seen_dot = 1;
            continue;
        }
        if (!isdigit(c)) {
            *reason = "rpm is not a decimal with at most 3 fractional digits";
            return -1;
        }
        seen_digit = 1;
        int digit = (int)(c - (unsigned char)'0');
        if (!seen_dot) {
            if (whole > 20000 || (whole == 20000 && digit > 0)) {
                *reason = "rpm exceeds 200000.000";
                return -1;
            }
            whole = whole * 10 + digit;
        } else {
            if (frac_digits == 3) {
                *reason = "rpm has more than 3 fractional digits";
                return -1;
            }
            frac = frac * 10 + digit;
            frac_digits++;
        }
    }
    if (!seen_digit) {
        *reason = "rpm is not a decimal with at most 3 fractional digits";
        return -1;
    }
    while (frac_digits < 3) {
        frac *= 10;
        frac_digits++;
    }
    if (whole == 200000 && frac > 0) {
        *reason = "rpm exceeds 200000.000";
        return -1;
    }
    *milli = whole * 1000 + frac;
    return 0;
}

static MsStatus copy_token(char *dst, size_t cap, const char *src, size_t n, const char *name,
                           int64_t line, const char *key, MsError *err) {
    if (n == 0U) {
        ms_set_error(err, name, line, "%s is empty", key);
        return MS_ERR_INPUT;
    }
    if (n >= cap) {
        ms_set_error(err, name, line, "%s exceeds %zu characters", key, cap - 1U);
        return MS_ERR_LIMIT;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 32U || c > 126U) {
            ms_set_error(err, name, line, "%s contains a character outside printable ASCII", key);
            return MS_ERR_INPUT;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return MS_OK;
}

static MsStatus field_text(const char *src, size_t n, char *dst, size_t cap, const char *name,
                           int64_t line, const char *label, MsError *err) {
    if (n >= cap) {
        ms_set_error(err, name, line, "%s exceeds %d characters", label, MS_MAX_FIELD_CHARS);
        return MS_ERR_LIMIT;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return MS_OK;
}

static int header_matches(const char *s, size_t n) {
    static const char hdr[] = "elapsed_ms,rpm,quality";
    return n == sizeof hdr - 1U && memcmp(s, hdr, sizeof hdr - 1U) == 0;
}

static MsStatus push_sample(MsSample **rows, size_t *count, size_t *cap, MsSample sample,
                            const char *name, int64_t line, MsError *err) {
    if (*count >= (size_t)MS_MAX_ROWS) {
        ms_set_error(err, name, line, "row limit of %d exceeded; split the log", MS_MAX_ROWS);
        return MS_ERR_LIMIT;
    }
    if (*count == *cap) {
        size_t next = (*cap == 0U) ? 16U : (*cap * 2U);
        if (next > (size_t)MS_MAX_ROWS) {
            next = (size_t)MS_MAX_ROWS;
        }
        MsSample *grown = realloc(*rows, next * sizeof(*grown));
        if (grown == NULL) {
            ms_set_error(err, name, line, "out of memory while reading rows");
            return MS_ERR_INTERNAL;
        }
        *rows = grown;
        *cap = next;
    }
    (*rows)[*count] = sample;
    *count += 1U;
    return MS_OK;
}

MsStatus ms_parse_csv(const char *data, size_t length, const char *name, MsSample **samples,
                      size_t *count, MsError *err) {
    MsSample *rows = NULL;
    size_t nrows = 0;
    size_t cap = 0;
    size_t index = 0;
    int64_t line_no = 1;
    int saw_header = 0;

    if (samples == NULL || count == NULL) {
        ms_set_error(err, name, 0, "internal parser call is missing an output pointer");
        return MS_ERR_INTERNAL;
    }
    *samples = NULL;
    *count = 0;
    if (data == NULL && length > 0U) {
        ms_set_error(err, name, 0, "internal parser call is missing a buffer");
        return MS_ERR_INTERNAL;
    }
    if (data == NULL) {
        data = "";
    }

    while (index < length) {
        const char *ptr = NULL;
        size_t len = 0;
        int got = 0;
        MsStatus st = next_line(data, length, &index, &line_no, name, &ptr, &len, &got, err);
        if (st != MS_OK) {
            free(rows);
            return st;
        }
        if (!got) {
            break;
        }
        if (len == 0U) {
            continue;
        }
        int64_t current = line_no - 1;
        if (!saw_header) {
            if (!header_matches(ptr, len)) {
                ms_set_error(err, name, current, "header must be elapsed_ms,rpm,quality");
                free(rows);
                return MS_ERR_INPUT;
            }
            saw_header = 1;
            continue;
        }

        size_t commas = 0;
        size_t cpos[2];
        for (size_t i = 0; i < len; i++) {
            if (ptr[i] == ',') {
                if (commas == 2U) {
                    ms_set_error(err, name, current,
                                 "expected 3 comma-separated fields (elapsed_ms,rpm,quality)");
                    free(rows);
                    return MS_ERR_INPUT;
                }
                cpos[commas++] = i;
            }
        }
        if (commas != 2U) {
            ms_set_error(err, name, current, "expected 3 comma-separated fields (elapsed_ms,rpm,quality)");
            free(rows);
            return MS_ERR_INPUT;
        }
        const char *fs[3];
        size_t fl[3];
        fs[0] = ptr;
        fl[0] = cpos[0];
        fs[1] = ptr + cpos[0] + 1U;
        fl[1] = cpos[1] - cpos[0] - 1U;
        fs[2] = ptr + cpos[1] + 1U;
        fl[2] = len - cpos[1] - 1U;
        for (int i = 0; i < 3; i++) {
            trim_span(&fs[i], &fl[i]);
        }

        char elapsed_text[MS_MAX_FIELD_CHARS + 1];
        char rpm_text[MS_MAX_FIELD_CHARS + 1];
        char quality_text[MS_MAX_FIELD_CHARS + 1];
        st = field_text(fs[0], fl[0], elapsed_text, sizeof elapsed_text, name, current, "elapsed_ms", err);
        if (st != MS_OK) {
            free(rows);
            return st;
        }
        st = field_text(fs[1], fl[1], rpm_text, sizeof rpm_text, name, current, "rpm", err);
        if (st != MS_OK) {
            free(rows);
            return st;
        }
        st = field_text(fs[2], fl[2], quality_text, sizeof quality_text, name, current, "quality", err);
        if (st != MS_OK) {
            free(rows);
            return st;
        }

        const char *reason = NULL;
        int64_t elapsed = 0;
        int64_t milli = 0;
        if (parse_i64(elapsed_text, 0, MS_MAX_ELAPSED_MS, &elapsed, &reason) != 0) {
            ms_set_error(err, name, current, "elapsed_ms %s", reason);
            free(rows);
            return MS_ERR_INPUT;
        }
        if (parse_rpm_milli(rpm_text, &milli, &reason) != 0) {
            ms_set_error(err, name, current, "%s", reason);
            free(rows);
            return MS_ERR_INPUT;
        }
        MsQuality quality;
        if (strcmp(quality_text, "ok") == 0) {
            quality = MS_QUALITY_OK;
        } else if (strcmp(quality_text, "invalid") == 0) {
            quality = MS_QUALITY_INVALID;
        } else if (strcmp(quality_text, "missing") == 0) {
            quality = MS_QUALITY_MISSING;
        } else {
            ms_set_error(err, name, current, "quality must be ok, invalid, or missing");
            free(rows);
            return MS_ERR_INPUT;
        }

        MsSample sample;
        sample.line = current;
        sample.elapsed_ms = elapsed;
        sample.milli_rpm = milli;
        sample.quality = quality;
        st = push_sample(&rows, &nrows, &cap, sample, name, current, err);
        if (st != MS_OK) {
            free(rows);
            return st;
        }
    }

    if (!saw_header) {
        ms_set_error(err, name, 0, "header must be elapsed_ms,rpm,quality");
        free(rows);
        return MS_ERR_INPUT;
    }
    if (nrows == 0U) {
        ms_set_error(err, name, 0, "log has no data rows");
        free(rows);
        return MS_ERR_INPUT;
    }
    for (size_t i = 1; i < nrows; i++) {
        if (rows[i].elapsed_ms == rows[i - 1U].elapsed_ms) {
            ms_set_error(err, name, rows[i].line, "duplicate timestamp %lld ms",
                         (long long)rows[i].elapsed_ms);
            free(rows);
            return MS_ERR_INPUT;
        }
        if (rows[i].elapsed_ms < rows[i - 1U].elapsed_ms) {
            ms_set_error(err, name, rows[i].line, "timestamps must be strictly increasing");
            free(rows);
            return MS_ERR_INPUT;
        }
    }

    *samples = rows;
    *count = nrows;
    return MS_OK;
}

enum SidecarKey {
    KEY_SCHEMA = 0,
    KEY_SAMPLING,
    KEY_MACHINE,
    KEY_REFERENCE,
    KEY_OFFSET,
    KEY_SKEW,
    KEY_UNCERTAINTY,
    KEY_TOLERANCE,
    KEY_RATE,
    KEY_PROVENANCE,
    KEY_CALIBRATION,
    KEY_TIME_BASE,
    KEY_COUNT
};

static const char *const SIDE_KEYS[KEY_COUNT] = {
    "schema_version",
    "sampling_interval_ms",
    "machine_source_id",
    "reference_source_id",
    "validated_time_offset_ms",
    "allowed_skew_ms",
    "reference_uncertainty_rpm",
    "comparison_tolerance_rpm",
    "max_reference_rate_rpm_per_s",
    "clock_alignment_provenance",
    "calibration_status",
    "time_base",
};

static int lookup_key(const char *text) {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (strcmp(text, SIDE_KEYS[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int line_is_comment(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    return i < n && s[i] == '#';
}

MsStatus ms_parse_sidecar(const char *data, size_t length, const char *name, MsConfig *cfg,
                          MsError *err) {
    int seen[KEY_COUNT];
    size_t index = 0;
    int64_t line_no = 1;

    if (cfg == NULL) {
        ms_set_error(err, name, 0, "internal sidecar call is missing an output pointer");
        return MS_ERR_INTERNAL;
    }
    memset(cfg, 0, sizeof(*cfg));
    memset(seen, 0, sizeof seen);
    if (data == NULL && length > 0U) {
        ms_set_error(err, name, 0, "internal sidecar call is missing a buffer");
        return MS_ERR_INTERNAL;
    }
    if (data == NULL) {
        data = "";
    }

    while (index < length) {
        const char *ptr = NULL;
        size_t len = 0;
        int got = 0;
        MsStatus st = next_line(data, length, &index, &line_no, name, &ptr, &len, &got, err);
        if (st != MS_OK) {
            return st;
        }
        if (!got) {
            break;
        }
        if (len == 0U || line_is_comment(ptr, len)) {
            continue;
        }
        int64_t current = line_no - 1;
        size_t eq = len;
        for (size_t i = 0; i < len; i++) {
            if (ptr[i] == '=') {
                eq = i;
                break;
            }
        }
        if (eq == len) {
            ms_set_error(err, name, current, "expected key=value");
            return MS_ERR_INPUT;
        }
        const char *ks = ptr;
        size_t kn = eq;
        const char *vs = ptr + eq + 1U;
        size_t vn = len - eq - 1U;
        trim_span(&ks, &kn);
        trim_span(&vs, &vn);
        char key[MS_MAX_FIELD_CHARS + 1];
        st = field_text(ks, kn, key, sizeof key, name, current, "sidecar key", err);
        if (st != MS_OK) {
            return st;
        }
        int id = lookup_key(key);
        if (id < 0) {
            ms_set_error(err, name, current, "unknown sidecar key '%s'", key);
            return MS_ERR_INPUT;
        }
        if (seen[id]) {
            ms_set_error(err, name, current, "duplicate sidecar key '%s'", key);
            return MS_ERR_INPUT;
        }
        seen[id] = 1;

        char value[MS_TEXT_CAP];
        const char *reason = NULL;
        if (id == KEY_MACHINE || id == KEY_REFERENCE || id == KEY_PROVENANCE || id == KEY_CALIBRATION ||
            id == KEY_TIME_BASE) {
            st = copy_token(value, sizeof value, vs, vn, name, current, key, err);
            if (st != MS_OK) {
                return st;
            }
        } else {
            st = field_text(vs, vn, value, sizeof value, name, current, key, err);
            if (st != MS_OK) {
                return st;
            }
        }

        if (id == KEY_SCHEMA) {
            int64_t schema = 0;
            if (parse_i64(value, 1, 1, &schema, &reason) != 0) {
                ms_set_error(err, name, current, "unsupported schema_version '%s'; supported value is 1",
                             value);
                return MS_ERR_UNSUPPORTED;
            }
            cfg->schema_version = (int32_t)schema;
        } else if (id == KEY_SAMPLING) {
            if (parse_i64(value, 1, MS_MAX_SAMPLING_MS, &cfg->sampling_interval_ms, &reason) != 0) {
                ms_set_error(err, name, current, "sampling_interval_ms %s", reason);
                return MS_ERR_INPUT;
            }
        } else if (id == KEY_OFFSET) {
            if (parse_i64(value, -MS_MAX_OFFSET_MS, MS_MAX_OFFSET_MS, &cfg->validated_time_offset_ms,
                          &reason) != 0) {
                ms_set_error(err, name, current, "validated_time_offset_ms %s", reason);
                return MS_ERR_INPUT;
            }
        } else if (id == KEY_SKEW) {
            if (parse_i64(value, 0, MS_MAX_SKEW_MS, &cfg->allowed_skew_ms, &reason) != 0) {
                ms_set_error(err, name, current, "allowed_skew_ms %s", reason);
                return MS_ERR_INPUT;
            }
        } else if (id == KEY_UNCERTAINTY || id == KEY_TOLERANCE || id == KEY_RATE) {
            int64_t milli = 0;
            if (parse_rpm_milli(value, &milli, &reason) != 0) {
                ms_set_error(err, name, current, "%s %s", key, reason);
                return MS_ERR_INPUT;
            }
            if (id == KEY_RATE && milli > MS_MAX_RATE_MILLI_PER_S) {
                ms_set_error(err, name, current, "max_reference_rate_rpm_per_s exceeds the supported range");
                return MS_ERR_INPUT;
            }
            if (id == KEY_UNCERTAINTY) {
                cfg->reference_uncertainty_milli_rpm = milli;
            } else if (id == KEY_TOLERANCE) {
                cfg->comparison_tolerance_milli_rpm = milli;
            } else {
                cfg->max_reference_rate_milli_rpm_per_s = milli;
            }
        } else if (id == KEY_MACHINE) {
            memcpy(cfg->machine_source_id, value, strlen(value) + 1U);
        } else if (id == KEY_REFERENCE) {
            memcpy(cfg->reference_source_id, value, strlen(value) + 1U);
        } else if (id == KEY_PROVENANCE) {
            if (strcmp(value, "undeclared") == 0) {
                ms_set_error(err, name, current,
                             "clock alignment provenance is required; refusing to compare without an "
                             "explicit validated time offset source");
                return MS_ERR_UNSUPPORTED;
            }
            memcpy(cfg->clock_alignment_provenance, value, strlen(value) + 1U);
        } else if (id == KEY_CALIBRATION) {
            if (strcmp(value, "synthetic-teaching-fixture") != 0 && strcmp(value, "reference-stated") != 0) {
                ms_set_error(err, name, current,
                             "unsupported calibration_status '%s'; supported values are "
                             "synthetic-teaching-fixture and reference-stated",
                             value);
                return MS_ERR_UNSUPPORTED;
            }
            memcpy(cfg->calibration_status, value, strlen(value) + 1U);
        } else if (id == KEY_TIME_BASE) {
            if (strcmp(value, "elapsed_ms") != 0) {
                ms_set_error(err, name, current,
                             "unsupported time_base '%s'; supported value is elapsed_ms", value);
                return MS_ERR_UNSUPPORTED;
            }
            memcpy(cfg->time_base, value, strlen(value) + 1U);
        }
    }

    for (int i = 0; i < KEY_COUNT; i++) {
        if (!seen[i]) {
            ms_set_error(err, name, 0, "missing sidecar key %s", SIDE_KEYS[i]);
            return MS_ERR_INPUT;
        }
    }
    return ms_validate_config(cfg, err);
}

static MsStatus load_file(const char *path, char **data, size_t *length, MsError *err) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ms_set_error(err, path, 0, "cannot open file (%s)", strerror(errno));
        return MS_ERR_INPUT;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        ms_set_error(err, path, 0, "cannot measure file size");
        fclose(fp);
        return MS_ERR_IO;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        ms_set_error(err, path, 0, "cannot measure file size");
        fclose(fp);
        return MS_ERR_IO;
    }
    if ((unsigned long)sz > (unsigned long)MS_MAX_FILE_BYTES) {
        ms_set_error(err, path, 0, "file exceeds %d byte limit", MS_MAX_FILE_BYTES);
        fclose(fp);
        return MS_ERR_LIMIT;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        ms_set_error(err, path, 0, "cannot rewind file");
        fclose(fp);
        return MS_ERR_IO;
    }
    char *buf = malloc((size_t)sz + 1U);
    if (buf == NULL) {
        ms_set_error(err, path, 0, "out of memory while reading file");
        fclose(fp);
        return MS_ERR_INTERNAL;
    }
    size_t nread = fread(buf, 1, (size_t)sz, fp);
    if (nread != (size_t)sz) {
        ms_set_error(err, path, 0, "short read while loading file");
        free(buf);
        fclose(fp);
        return MS_ERR_IO;
    }
    buf[nread] = '\0';
    fclose(fp);
    *data = buf;
    *length = nread;
    return MS_OK;
}

MsStatus ms_load_csv(const char *path, MsSample **samples, size_t *count, MsError *err) {
    char *data = NULL;
    size_t length = 0;
    MsStatus st = load_file(path, &data, &length, err);
    if (st != MS_OK) {
        return st;
    }
    st = ms_parse_csv(data, length, path, samples, count, err);
    free(data);
    return st;
}

MsStatus ms_load_sidecar(const char *path, MsConfig *cfg, MsError *err) {
    char *data = NULL;
    size_t length = 0;
    MsStatus st = load_file(path, &data, &length, err);
    if (st != MS_OK) {
        return st;
    }
    st = ms_parse_sidecar(data, length, path, cfg, err);
    free(data);
    return st;
}
