#!/usr/bin/env bash
# Check the Macrohelp task execution daemon; optionally start it.
# Usage: http-daemon-status.sh [--start]
set -euo pipefail

STATE_DIR="${MACROHELP_STATE_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp}"
DAEMON_DIR="$STATE_DIR/http-daemon"
BIN="$DAEMON_DIR/bin/macrohelp-httpd"
CTL_BIN="$STATE_DIR/overlay/bin/macrohelpctl"
ENV_FILE="$DAEMON_DIR/.env"
LOG_FILE="$DAEMON_DIR/httpd.log"
start=0
for arg in "$@"; do
  case "$arg" in
    --start) start=1 ;;
    -h|--help) sed -n '2,3p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

[ -x "$CTL_BIN" ] || { echo "cannot probe: rebuild the overlay client at $CTL_BIN" >&2; exit 1; }
probe() { "$CTL_BIN" controller-request --env "$ENV_FILE"; }
if body="$(probe)"; then
  echo "up: $body"
  exit 0
fi

if [ "$start" -eq 1 ]; then
  [ -x "$BIN" ] || { echo "cannot start: $BIN missing" >&2; exit 1; }
  [ -r "$ENV_FILE" ] || { echo "cannot start: $ENV_FILE missing" >&2; exit 1; }
  "$CTL_BIN" controller-request --env "$ENV_FILE" --port-free || exit 1
  nohup "$BIN" --env "$ENV_FILE" >>"$LOG_FILE" 2>&1 &
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    sleep 0.5
    if body="$(probe)"; then echo "started: $body"; exit 0; fi
  done
  echo "started but still unhealthy; see $LOG_FILE (including Omarsket socket errors)" >&2
fi
exit 1
