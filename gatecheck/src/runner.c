#include "gatecheck.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long long now_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static void sanitize_reason(char *buf) {
    for (size_t i = 0; buf[i] != '\0'; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') {
            buf[i] = ' ';
        }
    }
    size_t len = strlen(buf);
    while (len > 0 && buf[len - 1] == ' ') {
        buf[--len] = '\0';
    }
}

static int write_exact(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n > 0) {
            total += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static void run_single_script(const CheckSpec *spec, int check_id,
                              const char *input, size_t len, CheckMsg *msg) {
    memset(msg, 0, sizeof(*msg));
    msg->check_id = check_id;
    msg->critical = spec->critical;
    msg->exit_code = -1;
    snprintf(msg->name, sizeof(msg->name), "%s", spec->name);

    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) == -1) {
        snprintf(msg->reason, sizeof(msg->reason), "pipe(in) failed");
        return;
    }
    if (pipe(out_pipe) == -1) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        snprintf(msg->reason, sizeof(msg->reason), "pipe(out) failed");
        return;
    }

    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        snprintf(msg->reason, sizeof(msg->reason), "fork failed");
        return;
    }

    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);

        if (dup2(in_pipe[0], STDIN_FILENO) == -1 ||
            dup2(out_pipe[1], STDOUT_FILENO) == -1) {
            _exit(127);
        }

        close(in_pipe[0]);
        close(out_pipe[1]);

        char path_buf[GATECHECK_MAX_PATH];
        snprintf(path_buf, sizeof(path_buf), "%s", spec->path);
        char *const argv[] = {path_buf, NULL};
        execv(path_buf, argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    (void)set_nonblocking(in_pipe[1]);
    (void)set_nonblocking(out_pipe[0]);

    size_t written = 0;
    size_t reason_len = 0;
    int in_open = 1;
    int out_open = 1;
    int timed_out = 0;

    if (len == 0) {
        close(in_pipe[1]);
        in_open = 0;
    }

    long long deadline = now_monotonic_ms() + (long long)spec->timeout_ms;

    while (in_open || out_open) {
        long long rem = deadline - now_monotonic_ms();
        if (rem <= 0) {
            timed_out = 1;
            break;
        }

        struct pollfd pfds[2];
        int in_idx = -1;
        int out_idx = -1;
        nfds_t nfds = 0;

        if (in_open) {
            in_idx = (int)nfds;
            pfds[nfds].fd = in_pipe[1];
            pfds[nfds].events = POLLOUT;
            pfds[nfds].revents = 0;
            nfds++;
        }
        if (out_open) {
            out_idx = (int)nfds;
            pfds[nfds].fd = out_pipe[0];
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int rc = poll(pfds, nfds, (int)rem);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc == 0) {
            timed_out = 1;
            break;
        }

        if (in_idx != -1 && (pfds[in_idx].revents & (POLLOUT | POLLERR | POLLHUP))) {
            ssize_t w = write(in_pipe[1], input + written, len - written);
            if (w > 0) {
                written += (size_t)w;
                if (written >= len) {
                    close(in_pipe[1]);
                    in_open = 0;
                }
            } else if (!(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))) {
                close(in_pipe[1]);
                in_open = 0;
            }
        }

        if (out_idx != -1 && (pfds[out_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            char chunk[256];
            ssize_t r = read(out_pipe[0], chunk, sizeof(chunk));
            if (r > 0) {
                size_t cap = sizeof(msg->reason) - 1 - reason_len;
                size_t copy_n = ((size_t)r < cap) ? (size_t)r : cap;
                if (copy_n > 0) {
                    memcpy(msg->reason + reason_len, chunk, copy_n);
                    reason_len += copy_n;
                    msg->reason[reason_len] = '\0';
                }
            } else if (r == 0) {
                close(out_pipe[0]);
                out_open = 0;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                close(out_pipe[0]);
                out_open = 0;
            }
        }
    }

    if (in_open) {
        close(in_pipe[1]);
    }
    if (out_open) {
        close(out_pipe[0]);
    }

    int status = 0;
    pid_t wpid = 0;
    if (!timed_out) {
        while (1) {
            wpid = waitpid(pid, &status, WNOHANG);
            if (wpid == pid) {
                break;
            }
            if (wpid < 0 && errno != EINTR) {
                break;
            }
            if (now_monotonic_ms() >= deadline) {
                timed_out = 1;
                break;
            }
            struct timespec pause_ts = {0, 5000000L}; /* 5 ms */
            nanosleep(&pause_ts, NULL);
        }
    }

    if (timed_out) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        msg->timed_out = 1;
        msg->passed = 0;
        msg->exit_code = -1;
        snprintf(msg->reason, sizeof(msg->reason), "timed out after %d ms", spec->timeout_ms);
        return;
    }

    if (wpid == pid) {
        if (WIFEXITED(status)) {
            msg->exit_code = WEXITSTATUS(status);
            msg->passed = (msg->exit_code == 0) ? 1 : 0;
        } else if (WIFSIGNALED(status)) {
            msg->exit_code = 128 + WTERMSIG(status);
            msg->passed = 0;
            if (reason_len == 0) {
                snprintf(msg->reason, sizeof(msg->reason),
                         "terminated by signal %d", WTERMSIG(status));
            }
        }
    }

    sanitize_reason(msg->reason);
    if (msg->reason[0] == '\0') {
        snprintf(msg->reason, sizeof(msg->reason), "%s",
                 msg->passed ? "ok" : "check failed");
    }
}

void run_checker_process(int write_fd, const GateConfig *cfg, const char *input, size_t len) {
    /* Prevent script grandchildren from keeping monitor pipe open */
    (void)set_cloexec(write_fd);

    for (int i = 0; i < cfg->check_count; i++) {
        CheckMsg msg;
        run_single_script(&cfg->checks[i], i, input, len, &msg);

        if (write_exact(write_fd, &msg, sizeof(msg)) != 0) {
            break;
        }

        if (!msg.passed && msg.critical) {
            break; /* Fail-fast on critical check failure */
        }
    }

    close(write_fd);
    _exit(0);
}
