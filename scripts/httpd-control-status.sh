#!/usr/bin/env bash
# Check the Macrohelp overlay control API; optionally start its helper session.
# Usage: httpd-control-status.sh [--start]
# Backend connected:false is reported without failing the overlay health check.
set -euo pipefail

STATE_DIR="${MACROHELP_STATE_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp}"
CTL_BIN="$STATE_DIR/overlay/bin/macrohelpctl"
start=0
for arg in "$@"; do
  case "$arg" in
    --start) start=1 ;;
    -h|--help) sed -n '2,4p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

[ -x "$CTL_BIN" ] || { echo "cannot probe: rebuild the overlay client at $CTL_BIN" >&2; exit 1; }
options=()
env_file="$(cat "$STATE_DIR/env-path" 2>/dev/null || true)"
if [ -n "$env_file" ]; then options=(--env "$env_file"); fi
probe() { "$CTL_BIN" control-request "${options[@]}"; }
if body="$(probe)"; then
  echo "up: $body"
  exit 0
fi

if [ "$start" -eq 1 ]; then
  "$CTL_BIN" control-request "${options[@]}" --port-free || exit 1
  command -v omarchy >/dev/null || { echo "cannot start: enable Macrohelp in Omarchy, then use its tray Reconnect action" >&2; exit 1; }
  omarchy plugin enable macrohelp || exit 1
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    sleep 0.5
    if body="$(probe)"; then echo "started: $body"; exit 0; fi
  done
  echo "started but still unhealthy; use Macrohelp's tray Reconnect action and inspect the shell log" >&2
fi
exit 1
