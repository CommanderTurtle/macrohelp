#!/usr/bin/env bash
set -euo pipefail
SOURCE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd -- "$SOURCE/../.." && pwd)"
ENV_FILE="$REPO/.env"
OMARSKET=""
while (($#)); do
  case "$1" in
    --env|--omarsket) (($#>=2)) || { echo "Missing value for $1" >&2; exit 2; }; if [[ "$1" == --env ]]; then ENV_FILE="$2"; else OMARSKET="$2"; fi; shift 2 ;;
    -h|--help) echo 'bash http-daemon/wld/build.sh [--env PATH] [--omarsket EXISTING_CHECKOUT]'; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done
[[ "$(uname -s)" == Linux ]] || { echo 'Build inside Linux.' >&2; exit 1; }
for tool in cargo cc; do command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 1; }; done
TARGET="${XDG_CACHE_HOME:-$HOME/.cache}/macrohelp/http-daemon"
BIN="${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp/http-daemon/bin"
umask 077
mkdir -p "$BIN"
cargo build --locked --release --manifest-path "$SOURCE/Cargo.toml" --target-dir "$TARGET"
install -m 700 "$TARGET/release/macrohelp-httpd" "$BIN/macrohelp-httpd"
args=(--init --env "$ENV_FILE")
[[ -z "$OMARSKET" ]] || args+=(--omarsket "$OMARSKET")
"$BIN/macrohelp-httpd" "${args[@]}"
printf '\nStart the server manually:\n  %q --env %q\n' "$BIN/macrohelp-httpd" "$ENV_FILE"
