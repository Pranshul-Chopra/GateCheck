CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2 -Iinclude

SRCS = src/main.c src/config.c src/runner.c src/monitor.c
TARGET = gatecheck

.PHONY: all clean perms

all: $(TARGET)

$(TARGET): $(SRCS) include/gatecheck.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

perms:
	chmod +x checks.d/*.sh

clean:
	rm -f $(TARGET)
