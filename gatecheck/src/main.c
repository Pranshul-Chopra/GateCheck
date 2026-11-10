#include "gatecheck.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int read_stdin_bounded(char *buf, size_t max_len, size_t *out_len) {
    size_t total = 0;
    while (total < max_len) {
        size_t n = fread(buf + total, 1, max_len - total, stdin);
        total += n;
        if (n == 0) {
            if (ferror(stdin)) {
                return -1;
            }
            break;
        }
    }
    if (total == max_len) {
        int extra = fgetc(stdin);
        if (extra != EOF) {
            fprintf(stderr, "Input exceeds max size (%zu bytes)\n", max_len);
            return -1;
        }
    }
    *out_len = total;
    return 0;
}

static int wait_child(pid_t pid, int *status) {
    while (waitpid(pid, status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-c checks.conf] [-i \"input payload\"]\n"
                    "Reads input from stdin if -i is omitted.\n"
                    "Exit codes: 0=SAFE, 1=BLOCKED, 2=ERROR\n", prog);
}

int main(int argc, char **argv) {
    const char *config_path = "checks.conf";
    const char *inline_input = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            inline_input = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal(SIGPIPE)");
        return 2;
    }

    GateConfig cfg;
    if (load_gate_config(config_path, &cfg) != 0) {
        return 2;
    }

    char input_buf[GATECHECK_MAX_INPUT];
    const char *input_ptr = input_buf;
    size_t input_len = 0;

    if (inline_input != NULL) {
        input_ptr = inline_input;
        input_len = strlen(inline_input);
    } else if (read_stdin_bounded(input_buf, sizeof(input_buf), &input_len) != 0) {
        return 2;
    }

    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return 2;
    }

    fflush(stdout);

    pid_t p_checker = fork();
    if (p_checker < 0) {
        perror("fork checker");
        close(fd[0]);
        close(fd[1]);
        return 2;
    }
    if (p_checker == 0) {
        close(fd[0]);
        run_checker_process(fd[1], &cfg, input_ptr, input_len);
    }

    pid_t p_monitor = fork();
    if (p_monitor < 0) {
        perror("fork monitor");
        close(fd[0]);
        close(fd[1]);
        (void)wait_child(p_checker, NULL);
        return 2;
    }
    if (p_monitor == 0) {
        close(fd[1]);
        run_monitor_process(fd[0], cfg.threshold);
    }

    close(fd[0]);
    close(fd[1]);

    int st_checker = 0;
    int st_monitor = 0;
    (void)wait_child(p_checker, &st_checker);
    if (wait_child(p_monitor, &st_monitor) != 0) {
        return 2;
    }

    if (WIFEXITED(st_monitor)) {
        return WEXITSTATUS(st_monitor);
    }
    return 2;
}
