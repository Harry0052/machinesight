#include "machinesight/ms.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void print_help(FILE *fp) {
    fputs("machinesight " MS_VERSION
          "\n"
          "Compare a legacy machine RPM log with an independent reference log.\n"
          "\n"
          "Usage:\n"
          "  machinesight demo --output DIR\n"
          "  machinesight compare --machine FILE --reference FILE --sidecar FILE --output DIR\n"
          "  machinesight --help\n"
          "  machinesight --version\n"
          "\n"
          "The demo reads ./fixtures/demo. Run it from the repository root.\n"
          "A completed analysis writes summary.txt, findings.csv, and intervals.csv\n"
          "in DIR. The summary is also printed on standard output. Diagnostics go to\n"
          "standard error.\n"
          "\n"
          "Exit codes:\n"
          "  0  analysis completed\n"
          "  1  usage error\n"
          "  2  input rejected\n"
          "  3  output error\n"
          "  4  row, line, or file limit exceeded\n"
          "  5  unsupported calibration, time base, or clock provenance\n"
          "\n"
          "Disagreement is not a safe-to-operate decision and does not control equipment.\n",
          fp);
}

static int needs_value(int argc, char **argv, int index, const char *flag) {
    if (index + 1 >= argc || argv[index + 1][0] == '\0') {
        fprintf(stderr, "missing value for %s\n", flag);
        return -1;
    }
    return 0;
}

static int take_flag(int *seen, const char **slot, const char *value, const char *flag) {
    if (*seen) {
        fprintf(stderr, "repeated flag %s\n", flag);
        return -1;
    }
    if (strchr(value, '\n') != NULL || strchr(value, '\r') != NULL) {
        fprintf(stderr, "%s contains a newline\n", flag);
        return -1;
    }
    *seen = 1;
    *slot = value;
    return 0;
}

static MsStatus write_text_file(const char *dir, const char *name, const char *text, MsError *err) {
    char path[512];
    int wrote = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (wrote < 0 || (size_t)wrote >= sizeof path) {
        ms_set_error(err, dir, 0, "output path is too long");
        return MS_ERR_IO;
    }
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        ms_set_error(err, path, 0, "cannot write report (%s)", strerror(errno));
        return MS_ERR_IO;
    }
    size_t len = strlen(text);
    if (fwrite(text, 1, len, fp) != len) {
        ms_set_error(err, path, 0, "short write while saving the report");
        fclose(fp);
        return MS_ERR_IO;
    }
    if (fclose(fp) != 0) {
        ms_set_error(err, path, 0, "cannot finish writing the report");
        return MS_ERR_IO;
    }
    return MS_OK;
}

static MsStatus ensure_dir(const char *dir, MsError *err) {
    char buf[512];
    size_t n = strlen(dir);
    if (n == 0U || n >= sizeof buf) {
        ms_set_error(err, dir, 0, "output path is too long");
        return MS_ERR_IO;
    }
    memcpy(buf, dir, n + 1U);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] != '/') {
            continue;
        }
        buf[i] = '\0';
        if (buf[0] != '\0' && mkdir(buf, 0755) != 0 && errno != EEXIST) {
            ms_set_error(err, buf, 0, "cannot create output directory (%s)", strerror(errno));
            return MS_ERR_IO;
        }
        buf[i] = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        ms_set_error(err, buf, 0, "cannot create output directory (%s)", strerror(errno));
        return MS_ERR_IO;
    }
    struct stat st;
    if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ms_set_error(err, dir, 0, "output path is not a directory");
        return MS_ERR_IO;
    }
    return MS_OK;
}

static MsStatus run_compare(const char *machine_path, const char *reference_path, const char *sidecar_path,
                            const char *output, MsError *err) {
    MsConfig cfg;
    MsSample *machine = NULL;
    MsSample *reference = NULL;
    size_t machine_count = 0;
    size_t reference_count = 0;
    MsReport report;
    char *summary = NULL;
    char *findings = NULL;
    char *intervals = NULL;
    memset(&cfg, 0, sizeof cfg);
    memset(&report, 0, sizeof report);

    MsStatus st = ms_load_sidecar(sidecar_path, &cfg, err);
    if (st != MS_OK) {
        return st;
    }
    st = ms_load_csv(machine_path, &machine, &machine_count, err);
    if (st != MS_OK) {
        return st;
    }
    st = ms_load_csv(reference_path, &reference, &reference_count, err);
    if (st != MS_OK) {
        free(machine);
        return st;
    }
    st = ms_compare(&cfg, machine, machine_count, reference, reference_count, &report, err);
    free(machine);
    free(reference);
    if (st != MS_OK) {
        ms_report_free(&report);
        return st;
    }
    st = ms_render_summary(&cfg, &report, &summary, err);
    if (st == MS_OK) {
        st = ms_render_findings(&report, &findings, err);
    }
    if (st == MS_OK) {
        st = ms_render_intervals(&report, &intervals, err);
    }
    ms_report_free(&report);
    if (st != MS_OK) {
        free(summary);
        free(findings);
        free(intervals);
        return st;
    }
    st = ensure_dir(output, err);
    if (st == MS_OK) {
        st = write_text_file(output, "summary.txt", summary, err);
    }
    if (st == MS_OK) {
        st = write_text_file(output, "findings.csv", findings, err);
    }
    if (st == MS_OK) {
        st = write_text_file(output, "intervals.csv", intervals, err);
    }
    if (st == MS_OK) {
        fputs(summary, stdout);
        fprintf(stderr, "report_directory=%s\n", output);
    }
    free(summary);
    free(findings);
    free(intervals);
    return st;
}

int main(int argc, char **argv) {
    MsError err;
    memset(&err, 0, sizeof err);
    if (argc < 2) {
        print_help(stderr);
        return MS_ERR_USAGE;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "help") == 0) {
        print_help(stdout);
        return MS_OK;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
        puts("machinesight " MS_VERSION);
        return MS_OK;
    }

    const char *command = argv[1];
    int is_demo = strcmp(command, "demo") == 0;
    int is_compare = strcmp(command, "compare") == 0;
    if (!is_demo && !is_compare) {
        fprintf(stderr, "unknown command '%s'\n", command);
        print_help(stderr);
        return MS_ERR_USAGE;
    }

    const char *machine = NULL;
    const char *reference = NULL;
    const char *sidecar = NULL;
    const char *output = NULL;
    int saw_machine = 0;
    int saw_reference = 0;
    int saw_sidecar = 0;
    int saw_output = 0;
    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--output") == 0) {
            if (needs_value(argc, argv, i, arg) != 0) {
                return MS_ERR_USAGE;
            }
            if (take_flag(&saw_output, &output, argv[i + 1], arg) != 0) {
                return MS_ERR_USAGE;
            }
            i++;
        } else if (strcmp(arg, "--machine") == 0) {
            if (needs_value(argc, argv, i, arg) != 0) {
                return MS_ERR_USAGE;
            }
            if (take_flag(&saw_machine, &machine, argv[i + 1], arg) != 0) {
                return MS_ERR_USAGE;
            }
            i++;
        } else if (strcmp(arg, "--reference") == 0) {
            if (needs_value(argc, argv, i, arg) != 0) {
                return MS_ERR_USAGE;
            }
            if (take_flag(&saw_reference, &reference, argv[i + 1], arg) != 0) {
                return MS_ERR_USAGE;
            }
            i++;
        } else if (strcmp(arg, "--sidecar") == 0) {
            if (needs_value(argc, argv, i, arg) != 0) {
                return MS_ERR_USAGE;
            }
            if (take_flag(&saw_sidecar, &sidecar, argv[i + 1], arg) != 0) {
                return MS_ERR_USAGE;
            }
            i++;
        } else {
            fprintf(stderr, "unknown argument '%s'\n", arg);
            return MS_ERR_USAGE;
        }
    }

    if (!saw_output || output[0] == '\0') {
        fprintf(stderr, "missing required flag --output\n");
        return MS_ERR_USAGE;
    }
    if (is_demo && (saw_machine || saw_reference || saw_sidecar)) {
        fprintf(stderr, "demo does not accept log paths; it uses ./fixtures/demo\n");
        return MS_ERR_USAGE;
    }
    if (is_compare && (!saw_machine || !saw_reference || !saw_sidecar)) {
        fprintf(stderr, "compare requires --machine, --reference, and --sidecar\n");
        return MS_ERR_USAGE;
    }
    if (is_demo) {
        FILE *probe = fopen("fixtures/demo/machine.csv", "rb");
        if (probe == NULL) {
            fprintf(stderr, "demo fixtures not found in ./fixtures/demo; run this command from the repository root\n");
            return MS_ERR_INPUT;
        }
        fclose(probe);
        machine = "fixtures/demo/machine.csv";
        reference = "fixtures/demo/reference.csv";
        sidecar = "fixtures/demo/sidecar.txt";
    }

    MsStatus st = run_compare(machine, reference, sidecar, output, &err);
    if (st != MS_OK) {
        if (err.message[0] == '\0') {
            fprintf(stderr, "machinesight failed (%s)\n", ms_status_name(st));
        } else {
            fprintf(stderr, "%s\n", err.message);
        }
        return (int)st;
    }
    return MS_OK;
}
