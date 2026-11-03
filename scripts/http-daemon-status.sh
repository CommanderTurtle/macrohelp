#!/usr/bin/env bash
# Check the Macrohelp (Tasket) HTTP trigger daemon; optionally start it.
#
# Usage:
#   http-daemon-status.sh            # probe only; exit 0 healthy, 1 down
#   http-daemon-status.sh --start    # start macrohelp-httpd if down, re-probe
#
# This is the TASK EXECUTION httpd (macrohelp-httpd, default 127.0.0.1:7777,
# X-Tasket-Key auth from MACROHELP_API_KEY) -- NOT the overlay control plane
# on 7778 (see httpd-control-status.sh). It needs the Omarsket control
# socket: starts fail fast with a pointer if that socket is missing.
# After boot both services are typically down; run each script with --start.
set -euo pipefail

STATE_DIR="${MACROHELP_STATE_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp}"
DAEMON_DIR="$STATE_DIR/http-daemon"
BIN="$DAEMON_DIR/bin/macrohelp-httpd"
ENV_FILE="$DAEMON_DIR/.env"
LOG_FILE="$DAEMON_DIR/httpd.log"

start=0
for arg in "$@"; do
  case "$arg" in
    --start) start=1 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# Port + key come from the daemon's own .env (chmod 600 enforced by daemon).
port=7777
key=""
if [ -r "$ENV_FILE" ]; then
  port="$(sed -n 's/^MACROHELP_PORT=//p' "$ENV_FILE" | tail -1)"; port="${port:-7777}"
  key="$(sed -n 's/^MACROHELP_API_KEY=//p' "$ENV_FILE" | tail -1)"
fi

rt="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
NONCE_FILE="${MACROHELP_NONCE:-$rt/macrohelp/httpd.nonce}"

probe() {
  local body expected got
  # Fail closed: no readable handshake nonce means the daemon never started
  # (clean exits remove it), so refuse to hand the key to whoever answers.
  expected="$(cat "$NONCE_FILE" 2>/dev/null)" || expected=""
  if [ -z "$expected" ]; then
    echo "down: no handshake nonce at $NONCE_FILE — start macrohelp-httpd before probing" >&2
    reason=start
    return 1
  fi
  got="$(curl -fsS -m 3 "http://127.0.0.1:$port/challenge" 2>/dev/null \
    | sed -n 's/.*"nonce":"\([a-f0-9]*\)".*/\1/p')"
  if [ "$got" != "$expected" ]; then
    if [ -z "$got" ]; then
      echo "down: http://127.0.0.1:$port/challenge unreachable" >&2
      reason=down
    else
      echo "down: controller identity unverified (the port was claimed before startup)" >&2
      reason=identity
    fi
    return 1
  fi
  # --config keeps the API key off argv, where other UIDs could read it in
  # /proc/<pid>/cmdline; the key travels on stdin instead.
  body="$(curl -fsS -m 3 --config - <<CONFIG 2>/dev/null
url = "http://127.0.0.1:$port/health"
header = "X-Tasket-Key: $key"
CONFIG
)" || { echo "down: health failed after verified identity" >&2; reason=down; return 1; }
  # /health only answers once the Omarsket snapshot round-trip succeeds,
  # so a 2xx here proves the execution path, not just the listener.
  echo "$body"
}

if body="$(probe)"; then
  echo "up: http://127.0.0.1:$port $body"
  exit 0
fi

if [ "$start" -eq 1 ] && [ "${reason:-down}" = "identity" ]; then
  echo "not starting: port $port is claimed by a non-controller listener" >&2
  exit 1
fi

if [ "$start" -eq 1 ]; then
  [ -x "$BIN" ] || { echo "cannot start: $BIN missing" >&2; exit 1; }
  [ -r "$ENV_FILE" ] || { echo "cannot start: $ENV_FILE missing" >&2; exit 1; }
  socket="$(sed -n 's/^OMARSKET_SOCKET=//p' "$ENV_FILE" | tail -1)"
  socket="${socket:-${XDG_RUNTIME_DIR:?}/omarsket/control.sock}"
  if [ ! -S "$socket" ]; then
    echo "cannot start: Omarsket control socket $socket missing; start the Omarsket session first" >&2
    exit 1
  fi
  nohup "$BIN" --env "$ENV_FILE" >>"$LOG_FILE" 2>&1 &
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    sleep 0.5
    body="$(probe || true)" && { echo "started: $body"; exit 0; }
  done
  echo "started but still unhealthy; see $LOG_FILE" >&2
fi
exit 1
