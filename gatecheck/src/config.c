#include "gatecheck.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim_whitespace(char *s) {
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

static int parse_int_strict(const char *s, int *out) {
    if (s == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || val < INT_MIN || val > INT_MAX) {
        return -1;
    }
    *out = (int)val;
    return 0;
}

static int parse_check_spec(char *val, CheckSpec *spec) {
    char *c1 = strchr(val, ',');
    if (c1 == NULL) {
        return -1;
    }
    *c1 = '\0';
    char *name = trim_whitespace(val);
    char *rest = c1 + 1;

    char *c2 = strchr(rest, ',');
    char *path_raw = rest;
    char *crit_raw = NULL;
    char *timeout_raw = NULL;

    if (c2 != NULL) {
        *c2 = '\0';
        crit_raw = c2 + 1;
        char *c3 = strchr(crit_raw, ',');
        if (c3 != NULL) {
            *c3 = '\0';
            timeout_raw = c3 + 1;
        }
    }

    char *path = trim_whitespace(path_raw);
    if (*name == '\0' || *path == '\0') {
        return -1;
    }

    snprintf(spec->name, sizeof(spec->name), "%s", name);
    snprintf(spec->path, sizeof(spec->path), "%s", path);
    spec->critical = 0;
    spec->timeout_ms = GATECHECK_DEFAULT_TIMEOUT_MS;

    if (crit_raw != NULL) {
        char *crit_str = trim_whitespace(crit_raw);
        if (*crit_str != '\0') {
            int crit_val = 0;
            if (parse_int_strict(crit_str, &crit_val) != 0 || (crit_val != 0 && crit_val != 1)) {
                return -1;
            }
            spec->critical = crit_val;
        }
    }

    if (timeout_raw != NULL) {
        char *timeout_str = trim_whitespace(timeout_raw);
        if (*timeout_str != '\0') {
            int timeout_val = 0;
            if (parse_int_strict(timeout_str, &timeout_val) != 0 || timeout_val <= 0) {
                return -1;
            }
            spec->timeout_ms = timeout_val;
        }
    }

    return 0;
}

int load_gate_config(const char *config_path, GateConfig *out_cfg) {
    if (config_path == NULL || out_cfg == NULL) {
        return -1;
    }

    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->threshold = -1;

    FILE *fp = fopen(config_path, "r");
    if (fp == NULL) {
        perror("fopen checks.conf");
        return -1;
    }

    char line_buf[512];
    int line_no = 0;

    while (fgets(line_buf, sizeof(line_buf), fp) != NULL) {
        line_no++;
        char *line = trim_whitespace(line_buf);
        if (*line == '\0' || *line == '#') {
            continue;
        }

        if (strncmp(line, "threshold=", 10) == 0) {
            int thr = 0;
            if (parse_int_strict(trim_whitespace(line + 10), &thr) != 0 || thr <= 0) {
                fprintf(stderr, "Config error on line %d: invalid threshold\n", line_no);
                fclose(fp);
                return -1;
            }
            out_cfg->threshold = thr;
        } else if (strncmp(line, "check=", 6) == 0) {
            if (out_cfg->check_count >= GATECHECK_MAX_CHECKS) {
                fprintf(stderr, "Config error on line %d: max checks (%d) exceeded\n",
                        line_no, GATECHECK_MAX_CHECKS);
                fclose(fp);
                return -1;
            }
            if (parse_check_spec(line + 6, &out_cfg->checks[out_cfg->check_count]) != 0) {
                fprintf(stderr, "Config error on line %d: invalid check entry\n", line_no);
                fclose(fp);
                return -1;
            }
            out_cfg->check_count++;
        } else {
            fprintf(stderr, "Config error on line %d: unknown directive\n", line_no);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    if (out_cfg->threshold <= 0 || out_cfg->check_count <= 0) {
        fprintf(stderr, "Config error: must define threshold > 0 and at least 1 check\n");
        return -1;
    }

    return 0;
}
