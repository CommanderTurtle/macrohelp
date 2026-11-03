#!/usr/bin/env bash
# Check the Macrohelp overlay control httpd; optionally start it.
#
# Usage:
#   httpd-status.sh            # probe only; exit 0 healthy, 1 down
#   httpd-status.sh --start    # start macrohelpctl if down, then re-probe
#
# Probe: GET /v1/health on MACROHELP_CONTROL_PORT (default 7778) with
# Authorization: Bearer $(cat $STATE_DIR/control-key).
# Auth succeeds even when the omarsket backend shows connected:false;
# that field is reported but does not fail the check.
set -euo pipefail

STATE_DIR="${MACROHELP_STATE_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp}"
CTL_BIN="$STATE_DIR/overlay/bin/macrohelpctl"

port=7778
start=0
for arg in "$@"; do
  case "$arg" in
    --start) start=1 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# Port: MACROHELP_CONTROL_PORT in the overlay .env (control port; the
# plugin .env's MACROHELP_PORT belongs to the Tasket daemon, not this
# one), else 7778.
env_file="$(cat "$STATE_DIR/env-path" 2>/dev/null || true)"
if [ -n "$env_file" ] && [ -r "$env_file" ]; then
  port="$(sed -n 's/^MACROHELP_CONTROL_PORT=//p' "$env_file" | tail -1)"
fi
port="${port:-7778}"

rt="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
NONCE_FILE="${MACROHELP_CONTROL_NONCE:-$rt/macrohelp/control.nonce}"

probe() {
  local key expected got
  key="$(cat "$STATE_DIR/control-key" 2>/dev/null)" || {
    echo "down: no control key at $STATE_DIR/control-key" >&2
    reason=start
    return 1
  }
  # Fail closed: unless the listener echoes the owner-only handshake nonce,
  # it never sees the bearer token (an impostor that claimed the port first
  # would otherwise harvest it and replay it against the real overlay).
  expected="$(cat "$NONCE_FILE" 2>/dev/null)" || expected=""
  if [ -z "$expected" ]; then
    echo "down: no handshake nonce at $NONCE_FILE — start the overlay service before probing" >&2
    reason=start
    return 1
  fi
  got="$(curl -fsS -m 3 "http://127.0.0.1:$port/v1/challenge" 2>/dev/null \
    | sed -n 's/.*"nonce":"\([a-f0-9]*\)".*/\1/p')"
  if [ "$got" != "$expected" ]; then
    if [ -z "$got" ]; then
      echo "down: http://127.0.0.1:$port/v1/challenge unreachable" >&2
      reason=down
    else
      echo "down: overlay identity unverified (the port was claimed before startup)" >&2
      reason=identity
    fi
    return 1
  fi
  # --config keeps the bearer token off argv, where other UIDs could read it
  # in /proc/<pid>/cmdline; the key travels on stdin instead.
  curl -fsS -m 3 --config - <<CONFIG 2>/dev/null
url = "http://127.0.0.1:$port/v1/health"
header = "Authorization: Bearer $key"
CONFIG
}

if body="$(probe)"; then
  echo "up: http://127.0.0.1:$port $body"
  exit 0
fi

if [ "$start" -eq 1 ] && [ "${reason:-down}" = "identity" ]; then
  echo "not starting: port $port is claimed by a non-overlay listener" >&2
  exit 1
fi

if [ "$start" -eq 1 ]; then
  [ -x "$CTL_BIN" ] || { echo "cannot start: $CTL_BIN missing" >&2; exit 1; }
  nohup "$CTL_BIN" session >"$STATE_DIR/macrohelpctl.log" 2>&1 &
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    sleep 0.5
    body="$(probe || true)" && { echo "started: $body"; exit 0; }
  done
  echo "started but still unhealthy; see $STATE_DIR/macrohelpctl.log" >&2
fi
exit 1
