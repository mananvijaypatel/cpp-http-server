# cpp-http-server

A hand-written HTTP/1.1 server in C++20, built from raw POSIX sockets with no
web framework. Written to learn systems programming end to end: sockets, RAII,
concurrency, event-driven I/O, incremental protocol parsing, and the security
problems that come with accepting untrusted input from the network.

Handles **~69,000 requests/sec on a single thread**, holds hundreds of idle
connections in **4 MB of RSS**, and answers a request in **10 ms with 50 idle
connections open** — where an earlier thread-pool design took **4.2 seconds**
with only 8. All measured, all documented below, including where the numbers
contradicted expectations.

## Features

- **HTTP/1.1**: keep-alive, pipelined requests, `HEAD`, correct
  `Content-Length` handling
- **Single-threaded `epoll` event loop** with non-blocking I/O: connections are
  decoupled from threads, so idle clients cost a map entry rather than a
  blocked worker
- **Buffered, partial-aware I/O**: incomplete reads accumulate until a full
  request arrives; partial writes queue and resume on `EPOLLOUT`, so one slow
  client cannot stall the loop
- **Incremental parser** that handles TCP's stream nature: a request split
  across reads, or several requests arriving in one read
- **Security-minded parsing**: request-smuggling and header-DoS defenses,
  documented below
- **RAII throughout**: file descriptors cannot leak, and double-close is a
  compile error, not a runtime bug
- **Tested** with GoogleTest, including negative tests for malformed and
  hostile input
- **CI** runs Debug, AddressSanitizer, and ThreadSanitizer builds plus
  clang-format and clang-tidy on every push

## Quick start

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/server
```

```bash
curl -v http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/headers
curl -I http://127.0.0.1:8080/health
```

| Route | Methods | Description |
|---|---|---|
| `/` | GET, HEAD | HTML index page |
| `/health` | GET, HEAD | Liveness check, returns `ok` |
| `/headers` | GET, HEAD | Echoes the parsed request headers as plain text |
| `/favicon.ico` | GET, HEAD | `204 No Content` |

Unknown paths return `404`. Other methods return `405` with an `Allow` header.

## Architecture

```
                    ┌──────────────────────────────┐
   TCP clients ────►│  epoll_wait(timeout = 1s)    │  one thread
                    │  "which fds are ready now?"  │
                    └──────────┬───────────────────┘
                               │ [listener, fd 7, fd 203]
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
        accept4() all    service(fd 7)    service(fd 203)
        pending conns          │
                               ▼
        ┌──────────────────────────────────────────────┐
        │ recv until EAGAIN  ──►  conn.in              │
        │ parse_request_head ──►  Complete? route()    │
        │                         Incomplete? wait     │
        │ serialize          ──►  conn.out             │
        │ send what fits; keep the rest for EPOLLOUT   │
        └──────────────────────────────────────────────┘
                               │
                    sweep deadlines every 1s
```

Two details that are easy to get wrong:

**The buffer is parsed before `recv` is called.** A client can send several
requests in one packet, so data for the next request may already be buffered.
Reading first would block waiting for bytes that already arrived.

**Connection state lives in an explicit object, not on the stack.** With one
thread multiplexing many connections, each needs its own read buffer, pending
output, and deadline. This is the real cost of the event-driven model: you
hand-roll the state machine the call stack used to keep for you.

### Components

| File | Responsibility |
|---|---|
| `net/socket.hpp` | Move-only RAII file descriptor, `listen_tcp`, `send_all`, non-blocking mode |
| `net/event_loop.hpp` | RAII `epoll` wrapper: add / modify / remove / wait |
| `http/request.hpp` | Incremental request-head parser, connection policy |
| `http/response.hpp` | Response building and serialization |
| `http/router.hpp` | Pure function: `Request` → `Response` |
| `util/thread_pool.hpp` | Bounded worker pool (retained; used by tests, not the server) |

The parser and router perform no I/O. That is what made it possible to replace
the entire concurrency model — thread-per-connection, then a thread pool, then
an event loop — without changing a line of either.

## Security measures

| Threat | Defense |
|---|---|
| Request smuggling | Duplicate `Content-Length`/`Host` rejected; whitespace before `:` rejected; control characters rejected in targets and values |
| Header-based DoS | 8 KB cap on the request head, enforced *before* the terminator is found, so an endless header stream cannot grow the buffer |
| Body/next-request confusion | Requests carrying a body are answered and the connection closed, rather than risking body bytes being parsed as a following request |
| Idle connections | Per-connection deadline enforced by the event loop's 1-second sweep (15 s idle timeout) |
| Thread exhaustion | Connections are not bound to threads at all |
| fd exhaustion | `EMFILE` defers accepts rather than spinning on a failing `accept4` |
| Response splitting | CR/LF rejected in response header names and values, at the single point where headers are added |
| Reflected XSS | `/headers` served as `text/plain` with `X-Content-Type-Options: nosniff` |
| fd leak / double close | Move-only RAII `Socket`; copying is a compile error |
| SIGPIPE process kill | `MSG_NOSIGNAL` on every `send` |

Each row corresponds to at least one negative test in `tests/`.

## Benchmarks

Full results and methodology: [docs/benchmarks.md](docs/benchmarks.md).

| | Thread pool (8 threads) | epoll (1 thread) |
|---|---|---|
| Throughput @ 8 conns | 72,593 req/s | 68,753 req/s |
| Throughput @ 500 conns | 77,101 req/s | 65,813 req/s |
| p50 @ 500 conns | 72 us | 7.36 ms |
| p99 @ 8 conns | 53.87 ms | 1.21 ms |
| One request with 8 idle connections open | **4.20 s** | — |
| One request with 50 idle connections open | — | **0.01 s** |
| Reserved before serving anything | 64 MB of thread stacks | none |

**Throughput barely changed going from 8 threads to 1.** That was not the
expected result. The workload is syscall-bound rather than CPU-bound: parsing
a short request head costs far less than the `recv`/`send`/`epoll_wait` round
trips around it, and dropping 8 threads removed their context-switching and
cache contention.

**Latency shape changed a lot.** A single loop serves connections in sequence,
so p50 grows with concurrency. In exchange the tail is far more predictable:
p99/p50 is ~12x for the event loop at 8 connections, versus ~600x for the
thread pool, whose threads blocked each other unpredictably.

Loopback only, with `wrk` competing for the same 8 cores. See the benchmark
doc for the caveats.

## Known limitations

Each of these is deliberate, understood, and measured where possible.

- **One event loop means sequential service.** Throughput holds at ~66–75k
  req/s regardless of connection count, but p50 latency grows with concurrency
  (100 us at 8 connections, 7.36 ms at 500) because a single thread processes
  ready connections in order. One loop per core with `SO_REUSEPORT` would
  restore parallelism while keeping the memory profile.

- **The file-descriptor limit is the real connection ceiling.** With the common
  default of `ulimit -n 1024`, the server accepts roughly 1,020 connections
  regardless of how cheap each one is. `EMFILE` is handled by deferring accepts
  rather than spinning, but raising the limit (`LimitNOFILE`) is the actual fix.

- **Output buffers are unbounded.** A client that requests large responses and
  reads them slowly can grow `conn.out` without limit. A cap plus disconnection
  for clients that fall too far behind is the standard defense.

- **Level-triggered, not edge-triggered.** `EPOLLET` would reduce wakeups, and
  the read path already drains until `EAGAIN`, so the change is small.
  Level-triggered was chosen first because a missed drain under `EPOLLET` hangs
  a connection silently.

- **The timeout sweep is O(connections) every second.** Fine at hundreds; a
  deadline heap or timer wheel is the right structure at scale.

- **The idle timeout is not a whole-request deadline**, so a Slowloris client
  trickling one byte at a time still resets it. A per-request deadline is the
  fix, and the event loop now makes that straightforward.

- **No request bodies, no TLS, no static file serving, no compression, no
  HTTP/2.**

## Development

```bash
./scripts/format.sh                              # apply clang-format (pinned to v19)
cmake --preset asan && ctest --preset asan       # memory errors
cmake --preset tsan && ctest --preset tsan       # data races

run-clang-tidy-19 -p build -quiet \
  -header-filter='.*/cpp-http-server/(include|src)/.*' \
  '.*/cpp-http-server/(src|include)/.*'
```

clang-format and clang-tidy are pinned to version 19 in both the scripts and
CI. Different major versions format differently, which otherwise causes CI to
fail on code that passes locally.

## Roadmap

- [x] RAII sockets, TCP echo server
- [x] Bounded thread pool (mutex + condition variable)
- [x] HTTP/1.1 parsing, routing, keep-alive
- [x] CI matrix with sanitizers and static analysis
- [x] `epoll` event loop — non-blocking I/O, connections decoupled from threads
- [ ] One event loop per core (`SO_REUSEPORT`)
- [ ] Whole-request deadlines (Slowloris defense)
- [ ] Bounded output buffers
- [ ] Request bodies and `POST`
- [ ] Static file serving with MIME detection
- [ ] Graceful shutdown on `SIGTERM`

## License

MIT