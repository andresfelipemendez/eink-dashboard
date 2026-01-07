# eink-dashboard

C server for Sony DPT e-ink tablet dashboard with hot-reload development.

## Build

```bash
./build.sh      # Build loader (always GCC - TCC has ABI issues with lws)
./loader        # Run with hot-reload
```

Requires: `libwebsockets-devel`, `tcc` for instant app.so rebuilds.

## Architecture

```
loader (static)          app.so (hot-reload)
├── lws_context          ├── handle_http()
├── http callback   ──►  ├── get_time()
├── livereload ws        └── on_reload()
├── time ws + timer
└── inotify watcher
```

- **loader** - Owns socket, callbacks, persists across reloads
- **app.so** - Pure handler functions, rebuilt on file change
- Edit `src/server.c`, save, browser auto-refreshes

## WebSocket Endpoints

| Protocol | Purpose |
|----------|---------|
| `livereload` | Browser connects; closes on reload to trigger refresh |
| `time` | Pushes server time every second |

## Docker

```bash
docker build -t eink-dashboard .
docker run -p 8080:8080 eink-dashboard
```

## Files

```
src/
├── loader.c       # Dev: socket, watcher, hot-reload
├── loader_prod.c  # Prod: no watcher, no hot-reload
├── app.c          # Unity build entry point
├── app.h          # AppAPI interface
└── server.c       # Handler implementations
```

## License

MIT
