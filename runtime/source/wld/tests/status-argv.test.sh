#!/usr/bin/env bash
# Regression: status probes must never place API keys in process arguments
# (readable by other local UIDs via /proc/<pid>/cmdline), and they must
# challenge the listener against the owner-only handshake nonce before any
# request carries the key. An impostor that claimed the port first, or a
# service that never wrote its nonce, must leave the key at home entirely.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
KEY='argv-canary-key-0123456789'
REAL='deadbeefcafe0123456789abcdef0000'

mkdir -p "$TMP/bin" "$TMP/state/http-daemon"
cat >"$TMP/bin/curl" <<'SHIM'
#!/bin/sh
# Each curl call appends its argv to $ARGV_FILE and its stdin to $STDIN_FILE.
for arg in "$@"; do printf '%s\n' "$arg" >>"$ARGV_FILE"; done
printf -- '---\n' >>"$ARGV_FILE"
case "$*" in
  *--config*)
    cat >>"$STDIN_FILE"
    printf -- '\n---\n' >>"$STDIN_FILE"
    printf '{"success":true,"data":{"status":"ok"}}'
    ;;
  *)
    # A challenge: a genuine controller echoes the nonce file it can read.
    if [ "$SHIM_MODE" = ok ]; then
      printf '{"success":true,"data":{"nonce":"%s"}}' "$(cat "$SHIM_NONCE_FILE")"
    else
      printf '{"success":true,"data":{"nonce":"ffffffffffffffffffffffffffffffff"}}'
    fi
    ;;
esac
SHIM
chmod +x "$TMP/bin/curl"
printf 'MACROHELP_PORT=17777\nMACROHELP_API_KEY=%s\n' "$KEY" >"$TMP/state/http-daemon/.env"
printf '%s' "$KEY" >"$TMP/state/control-key"
printf '%s\n' "$REAL" >"$TMP/httpd.nonce"
printf '%s\n' "$REAL" >"$TMP/control.nonce"

run() { # script mode nonce-file [have-nonce:yes|no]
  local script="$1" mode="$2" nonce="$3" have="${4:-yes}"
  rm -f "$TMP/argv" "$TMP/stdin" "$TMP/out"
  : >"$TMP/argv"
  : >"$TMP/stdin"
  [ "$have" = yes ] || rm -f "$nonce"
  SHIM_MODE="$mode" SHIM_NONCE_FILE="$nonce" \
  ARGV_FILE="$TMP/argv" STDIN_FILE="$TMP/stdin" \
  MACROHELP_STATE_DIR="$TMP/state" \
  MACROHELP_NONCE="$TMP/httpd.nonce" MACROHELP_CONTROL_NONCE="$TMP/control.nonce" \
  PATH="$TMP/bin:$PATH" \
    bash "$ROOT/scripts/$script" >"$TMP/out" 2>&1
  return $?
}

assert_no_key_anywhere() { # label
  if grep -q "$KEY" "$TMP/argv" "$TMP/stdin"; then
    echo "$1: KEY REACHED AN UNVERIFIED LISTENER" >&2
    cat "$TMP/argv" "$TMP/stdin" >&2
    exit 1
  fi
}

# 1. Verified controller: up; key travels via --config stdin, never argv.
for script in http-daemon-status.sh httpd-control-status.sh; do
  nonce="$TMP/httpd.nonce"
  [ "$script" = httpd-control-status.sh ] && nonce="$TMP/control.nonce"
  run "$script" ok "$nonce"
  if grep -q "$KEY" "$TMP/argv"; then
    echo "$script: KEY LEAKED INTO ARGV" >&2
    cat "$TMP/argv" >&2
    exit 1
  fi
  if ! grep -qE "Authorization: Bearer $KEY|X-Tasket-Key: $KEY" "$TMP/stdin"; then
    echo "$script: key never reached the request (expected via --config stdin)" >&2
    exit 1
  fi
  echo "$script: verified controller, argv clean, key delivered via stdin"
done

# 2. Impostor answering the challenge: refuse, key nowhere at all.
run http-daemon-status.sh bad "$TMP/httpd.nonce" && {
  echo "daemon impostor run unexpectedly succeeded" >&2
  exit 1
}
grep -q 'identity unverified' "$TMP/out" || {
  echo "daemon impostor: expected identity-unverified refusal" >&2
  cat "$TMP/out" >&2
  exit 1
}
assert_no_key_anywhere "http-daemon-status impostor"
echo "http-daemon-status.sh: impostor refused, key never sent"

run httpd-control-status.sh bad "$TMP/control.nonce" && {
  echo "control impostor run unexpectedly succeeded" >&2
  exit 1
}
grep -q 'identity unverified' "$TMP/out" || {
  echo "control impostor: expected identity-unverified refusal" >&2
  cat "$TMP/out" >&2
  exit 1
}
assert_no_key_anywhere "httpd-control-status impostor"
echo "httpd-control-status.sh: impostor refused, key never sent"

# 3. No handshake nonce at all (service never started): fail closed, no curl.
run http-daemon-status.sh ok "$TMP/httpd.nonce" no && {
  echo "daemon absent-nonce run unexpectedly succeeded" >&2
  exit 1
}
grep -q 'no handshake nonce' "$TMP/out" || {
  echo "daemon absent-nonce: expected fail-closed message" >&2
  cat "$TMP/out" >&2
  exit 1
}
assert_no_key_anywhere "http-daemon-status absent nonce"
echo "http-daemon-status.sh: absent nonce, fail closed"

run httpd-control-status.sh ok "$TMP/control.nonce" no && {
  echo "control absent-nonce run unexpectedly succeeded" >&2
  exit 1
}
grep -q 'no handshake nonce' "$TMP/out" || {
  echo "control absent-nonce: expected fail-closed message" >&2
  cat "$TMP/out" >&2
  exit 1
}
assert_no_key_anywhere "httpd-control-status absent nonce"
echo "httpd-control-status.sh: absent nonce, fail closed"
