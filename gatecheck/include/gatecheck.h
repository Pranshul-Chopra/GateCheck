#ifndef GATECHECK_H
#define GATECHECK_H

#include <stddef.h>

#define GATECHECK_MAX_CHECKS 32
#define GATECHECK_MAX_PATH 256
#define GATECHECK_MAX_NAME 64
#define GATECHECK_MAX_REASON 192
#define GATECHECK_MAX_INPUT 65536
#define GATECHECK_DEFAULT_TIMEOUT_MS 1000

typedef struct {
    char name[GATECHECK_MAX_NAME];
    char path[GATECHECK_MAX_PATH];
    int critical;
    int timeout_ms;
} CheckSpec;

typedef struct {
    int threshold;
    int check_count;
    CheckSpec checks[GATECHECK_MAX_CHECKS];
} GateConfig;

/* Fixed-size message (< 512 bytes) for atomic POSIX pipe writes */
typedef struct {
    int check_id;
    int passed;
    int critical;
    int timed_out;
    int exit_code;
    char name[GATECHECK_MAX_NAME];
    char reason[GATECHECK_MAX_REASON];
} CheckMsg;

int load_gate_config(const char *config_path, GateConfig *out_cfg);
void run_checker_process(int write_fd, const GateConfig *cfg, const char *input, size_t len);
void run_monitor_process(int read_fd, int threshold);

#endif
