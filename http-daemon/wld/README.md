# Wayland controller

Build and usage: [Quick start](../../quick-start.md).
Rust modules mirror the Qt daemon's HttpServer, TaskRegistry, TaskRunner,
TaskJsonLoader, TaskExecutor and Types. Input execution remains in Omarsket;
the overlay is a separate client under `runtime/source/wld`, not part of this build.

Checks (no input generated):

```sh
OMARSKET_TEST_BIN=/path/to/omarsket cargo test --locked
```

HTTP contract tests start an actual Omarsket `--dry-run` process, exercise every
route through loopback HTTP and compare the route inventory against the Qt source.
Omarsket's Unix socket tests run on Linux. A real Linux session is still needed
to check compositor permissions and actual input.
