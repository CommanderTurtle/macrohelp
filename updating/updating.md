# Updating (after pull)

One-shot re-initialization after `git pull` in this checkout.
Prerequisite: initial setup done once per `quick-start.md` (both builds run
once, plugin enabled, httpd started).

Usage, from the checkout root (after omarsket's `updating/after-pull.sh`):

    bash updating/after-pull.sh

Sequence (the canonical flow, nothing else):

1. `bash http-daemon/wld/build.sh --omarsket <omarsket checkout>`
   (the sibling `../io.github.commanderturtle.omarsket`, derived at run time)
2. `bash runtime/source/wld/build.sh`
3. Stop the running (stale) `macrohelp-httpd` with TERM, then start the fresh
   binary:
   `nohup "$XDG_DATA_HOME/macrohelp/http-daemon/bin/macrohelp-httpd" --env <checkout>/.env >> $XDG_DATA_HOME/macrohelp/http-daemon/httpd.log &`
4. `omarchy restart shell` (loads the new overlay client and the new omarsket runtime)

Notes:

- The `input.lua` dofile line printed by the overlay build only changes if the
  checkout path moves. `~/.config/hypr/autostart.lua` already includes it, so
  no `hyprctl reload` is part of this flow.
- The `.env` is the one in this checkout root (the path the build script prints).
- No tests. If `macrohelp-httpd` was not running, step 3 simply starts it.
