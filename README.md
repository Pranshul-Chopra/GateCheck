# gatecheck

Lightweight C framework for running pluggable server-side input/log/registration checks in isolated POSIX subprocesses with `+1` threshold scoring.

## Architecture

1. **Orchestrator (`src/main.c`)** loads `checks.conf`, reads the raw payload (`-i` or `stdin`), opens a pipe, and spawns two subprocesses.
2. **Checker Subprocess (`src/runner.c`)** executes each configured external check script (`fork` + `execv` without a shell), feeds the raw payload safely over `stdin`, enforces a per-script `timeout_ms` via `poll()` + `SIGKILL`, and streams `CheckMsg` structs over the pipe.
3. **Monitor Subprocess (`src/monitor.c`)** tallies `+1` for every passed check (`exit 0`), checks `score >= threshold` with no critical failures, prints the check log, and exits `0` (`SAFE`) or `1` (`BLOCKED`).

## Plugin Script Contract

Any language works (Bash, Python, Go, compiled binary). Each script simply:
1. Reads the raw input from `stdin`.
2. Optionally prints a short single-line reason to `stdout`.
3. Exits `0` to pass (`+1`) or non-zero to fail (`+0`).

Configure checks and threshold in `checks.conf`:

```ini
threshold=3
check=01_format,./checks.d/01_format.sh,1,1000
check=02_no_secrets,./checks.d/02_no_secrets.sh,0,1000
check=03_registry_safe,./checks.d/03_registry_safe.sh,1,1500
```

## Build & Run (Linux / POSIX)

```bash
make
make perms
./gatecheck -i "INFO user=42 action=login status=200"
./gatecheck -i "INFO user=42 api_key=secret123"
echo "INFO user=42 action=register" | ./gatecheck
```

Exit codes: `0` = `SAFE`, `1` = `BLOCKED`, `2` = configuration/runtime error.
