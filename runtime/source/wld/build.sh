#!/usr/bin/env bash
set -euo pipefail
SOURCE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" == --help ]]; then echo 'bash runtime/source/wld/build.sh'; exit 0; fi
(($#==0)) || { echo 'No arguments expected; configuration belongs in .env.' >&2; exit 2; }
[[ "$(uname -s)" == Linux ]] || { echo 'Build inside Linux.' >&2; exit 1; }
for tool in cargo cc; do command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 1; }; done
TARGET="${XDG_CACHE_HOME:-$HOME/.cache}/macrohelp/overlay"
BIN="${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp/overlay/bin"
umask 077
mkdir -p "$BIN"
cargo build --locked --release --manifest-path "$SOURCE/Cargo.toml" --target-dir "$TARGET"
install -m 700 "$TARGET/release/macrohelpctl" "$BIN/macrohelpctl"
printf 'Overlay client built. Enable io.github.commanderturtle.macrohelp, or run omarchy-restart-shell if already loaded.\n'
printf 'No HTTP server was built, installed or started.\n'
