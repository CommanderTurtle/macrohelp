#!/usr/bin/env bash
# Status helpers delegate auth/configuration to the peer-checked native client.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
KEY='argv-canary-key-0123456789'
mkdir -p "$TMP/bin" "$TMP/state/overlay/bin" "$TMP/state/http-daemon/bin"
cat >"$TMP/state/overlay/bin/macrohelpctl" <<'SHIM'
#!/bin/bash
printf '%s\n' "$@" >>"$ARGV_FILE"
if [[ "$*" == *--port-free* ]]; then
  [[ "$SHIM_MODE" != claimed ]]
  exit $?
fi
if [[ "$1" == session ]]; then exit 0; fi
count="$(cat "$COUNT_FILE" 2>/dev/null || echo 0)"
count=$((count+1))
printf '%s' "$count" >"$COUNT_FILE"
if [[ "$SHIM_MODE" == ok ]] || [[ "$SHIM_MODE" == delayed && "$count" -ge 4 ]]; then
  echo '{"ready":true,"connected":false}'
  exit 0
fi
echo 'identity unverifiable; key withheld' >&2
exit 1
SHIM
printf '#!/bin/sh\nexit 0\n' >"$TMP/state/http-daemon/bin/macrohelp-httpd"
printf '#!/bin/sh\nexit 0\n' >"$TMP/bin/sleep"
printf '#!/bin/sh\n[ "$*" = "plugin enable macrohelp" ]\n' >"$TMP/bin/omarchy"
chmod +x "$TMP/state/overlay/bin/macrohelpctl" "$TMP/state/http-daemon/bin/macrohelp-httpd" "$TMP/bin/sleep" "$TMP/bin/omarchy"
printf 'MACROHELP_PORT=17777\nMACROHELP_API_KEY=%s\n' "$KEY" >"$TMP/state/http-daemon/.env"
printf '%s' "$KEY" >"$TMP/state/control-key"
printf '%s\n' "$TMP/state/http-daemon/.env" >"$TMP/state/env-path"
run() {
  local script="$1" mode="$2"; shift 2
  : >"$TMP/argv"
  printf 0 >"$TMP/count"
  SHIM_MODE="$mode" ARGV_FILE="$TMP/argv" COUNT_FILE="$TMP/count" \
  MACROHELP_STATE_DIR="$TMP/state" PATH="$TMP/bin:$PATH" \
    bash "$ROOT/scripts/$script" "$@" >"$TMP/out" 2>&1
}
for script in http-daemon-status.sh httpd-control-status.sh; do
  run "$script" ok
  grep -q 'up:' "$TMP/out"
  if grep -q "$KEY" "$TMP/argv" "$TMP/out"; then echo 'key exposed' >&2; exit 1; fi
  grep -q -- '--env' "$TMP/argv"

  if run "$script" down; then echo 'unavailable probe succeeded' >&2; exit 1; fi
  grep -q 'key withheld' "$TMP/out"
  if grep -q "$KEY" "$TMP/argv" "$TMP/out"; then echo 'key exposed' >&2; exit 1; fi

  run "$script" delayed --start
  grep -q 'started:' "$TMP/out"
  [[ "$(cat "$TMP/count")" == 4 ]]

  if run "$script" down --start; then echo 'failed retries reported success' >&2; exit 1; fi
  [[ "$(cat "$TMP/count")" == 11 ]]
  grep -q 'still unhealthy' "$TMP/out"

  if run "$script" claimed --start; then echo 'claimed port accepted' >&2; exit 1; fi
  [[ "$(cat "$TMP/count")" == 1 ]]
  if grep -q "$KEY" "$TMP/argv" "$TMP/out"; then echo 'key exposed' >&2; exit 1; fi
  echo "$script: native probe argv, fail-closed status and bounded retries"
done
