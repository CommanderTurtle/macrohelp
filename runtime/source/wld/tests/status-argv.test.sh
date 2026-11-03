#!/usr/bin/env bash
# Regression: status probes must never place API keys in process arguments,
# where other local UIDs could read them from /proc/<pid>/cmdline.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
KEY='argv-canary-key-0123456789'

mkdir -p "$TMP/bin" "$TMP/state/http-daemon"
cat >"$TMP/bin/curl" <<'SHIM'
#!/bin/sh
: >"$ARGV_FILE"
for arg in "$@"; do printf '%s\n' "$arg" >>"$ARGV_FILE"; done
cat >"$STDIN_FILE"
printf '{"success":true,"data":{"status":"ok"}}'
SHIM
chmod +x "$TMP/bin/curl"
printf 'MACROHELP_PORT=17777\nMACROHELP_API_KEY=%s\n' "$KEY" >"$TMP/state/http-daemon/.env"
printf '%s' "$KEY" >"$TMP/state/control-key"

check() { # label argv stdin
  local label="$1" argv="$2" stdin="$3"
  if grep -q "$KEY" "$argv"; then
    echo "$label: KEY LEAKED INTO ARGV" >&2
    cat "$argv" >&2
    exit 1
  fi
  if ! grep -q "$KEY" "$stdin"; then
    echo "$label: key never reached the request (expected via --config stdin)" >&2
    exit 1
  fi
  echo "$label: argv clean, key delivered via stdin"
}

for script in http-daemon-status.sh httpd-control-status.sh; do
  ARGV_FILE="$TMP/argv-$script" STDIN_FILE="$TMP/stdin-$script" \
  MACROHELP_STATE_DIR="$TMP/state" PATH="$TMP/bin:$PATH" \
    bash "$ROOT/scripts/$script" >/dev/null
  check "$script" "$TMP/argv-$script" "$TMP/stdin-$script"
done
