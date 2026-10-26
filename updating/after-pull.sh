#!/bin/sh
# Re-initialize macrohelp after pulling changes into this checkout.
# Run from the checkout root: bash updating/after-pull.sh
# Zero frills: this is the canonical update sequence, no tests.

set -eu

cd "$(dirname "$0")/.."

# Omarsket is the sibling checkout under the same plugins/ directory.
omarsket_dir="$(cd ../io.github.commanderturtle.omarsket && pwd)"

bash http-daemon/wld/build.sh --omarsket "$omarsket_dir"
bash runtime/source/wld/build.sh

# Restart the running (stale) httpd with the fresh binary, same --env.
httpd_bin="${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp/http-daemon/bin/macrohelp-httpd"
httpd_log="${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp/http-daemon/httpd.log"
pkill -f "macrohelp-httpd" || true
sleep 1
nohup "$httpd_bin" --env "$PWD/.env" >> "$httpd_log" 2>&1 &

omarchy restart shell
