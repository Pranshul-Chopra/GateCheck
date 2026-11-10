#include "gatecheck.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Returns 1 on full struct read, 0 on clean EOF, -1 on truncated/error read */
static int read_msg_exact(int fd, CheckMsg *msg) {
    char *buf = (char *)msg;
    size_t total = 0;

    while (total < sizeof(*msg)) {
        ssize_t n = read(fd, buf + total, sizeof(*msg) - total);
        if (n > 0) {
            total += (size_t)n;
        } else if (n == 0) {
            return (total == 0) ? 0 : -1;
        } else if (errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 1;
}

void run_monitor_process(int read_fd, int threshold) {
    CheckMsg msg;
    int score = 0;
    int failed = 0;
    int critical_failed = 0;
    int rc;

    while ((rc = read_msg_exact(read_fd, &msg)) == 1) {
        if (msg.passed) {
            score += 1;
            printf("[Monitor] #%d %-16s PASS (+1) -> %d/%d | %s\n",
                   msg.check_id, msg.name, score, threshold, msg.reason);
        } else {
            failed += 1;
            if (msg.critical) {
                critical_failed = 1;
            }
            const char *tag = msg.timed_out ? " [TIMEOUT]" :
                              (msg.critical ? " [CRITICAL]" : "");
            printf("[Monitor] #%d %-16s FAIL (+0) -> %d/%d%s | %s\n",
                   msg.check_id, msg.name, score, threshold, tag, msg.reason);
        }
    }

    if (rc < 0) {
        fprintf(stderr, "[Monitor] Error reading from checker pipe\n");
        fflush(stderr);
        close(read_fd);
        _exit(2);
    }

    int safe = (score >= threshold) && !critical_failed;
    printf("[Monitor] Verdict: %s (score=%d/%d, failed=%d, critical_failed=%d)\n",
           safe ? "SAFE" : "BLOCKED", score, threshold, failed, critical_failed);

    fflush(stdout);
    close(read_fd);
    _exit(safe ? 0 : 1);
}
