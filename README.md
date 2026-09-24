# cpp-http-server

A hand-written HTTP/1.1 server in C++20, built from raw POSIX sockets with no
web framework. Written to learn systems programming end to end: sockets,
RAII, concurrency, incremental protocol parsing, and the security problems
that come with accepting untrusted input from the network.

Handles **77,000 requests/sec** with a **252us p99** at 500 concurrent
connections on loopback — and has a measured failure mode that eight idle
`nc` sessions can trigger. Both are documented below.

## Features

- **HTTP/1.1**: keep-alive, pipelined requests, `HEAD`, correct
  `Content-Length` handling
- **Bounded thread pool** (mutex + condition variable) so connection load
  cannot exhaust system threads
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
                  ┌──────────────┐
   TCP clients ──►│  listen(2)   │  main thread: accept() only
                  └──────┬───────┘
                         │ Socket moved into a task
                  ┌──────▼───────┐
                  │  task queue  │  mutex + condition_variable
                  └──────┬───────┘
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
     worker 1       worker 2   ...  worker N     (N = CPU cores)
          │
          ▼
   ┌──────────────────────────────────────────┐
   │  parse buffer ──► Incomplete? ──► recv() │
   │        │                           │     │
   │        │ Complete            ──────┘     │
   │        ▼                                 │
   │    route(req) ──► serialize ──► send_all │
   │        │                                 │
   │   keep-alive? loop : close               │
   └──────────────────────────────────────────┘
```

The loop parses the buffer *before* calling `recv()`. A client can send
several requests in one packet, so data for the next request may already be
buffered; reading first would block waiting for bytes that already arrived.

### Components

| File | Responsibility |
|---|---|
| `net/socket.hpp` | Move-only RAII file descriptor, `listen_tcp`, `send_all`, timeouts |
| `util/thread_pool.hpp` | Bounded worker pool over a shared task queue |
| `http/request.hpp` | Incremental request-head parser, connection policy |
| `http/response.hpp` | Response building and serialization |
| `http/router.hpp` | Pure function: `Request` → `Response` |

The parser and router perform no I/O, which is what makes them
straightforward to unit test — including with deliberately malformed input.

## Security measures

| Threat | Defense |
|---|---|
| Request smuggling | Duplicate `Content-Length`/`Host` rejected; whitespace before `:` rejected; control characters rejected in targets and values |
| Header-based DoS | 8 KB cap on the request head, enforced *before* the terminator is found, so an endless header stream cannot grow the buffer |
| Body/next-request confusion | Requests carrying a body are answered and the connection closed, rather than risking body bytes being parsed as a following request |
| Idle connections | `SO_RCVTIMEO` closes silent clients (partial — see limitations) |
| Thread exhaustion | Fixed worker pool instead of thread-per-connection |
| Response splitting | CR/LF rejected in response header names and values, at the single point where headers are added |
| Reflected XSS | `/headers` served as `text/plain` with `X-Content-Type-Options: nosniff` |
| fd leak / double close | Move-only RAII `Socket`; copying is a compile error |
| SIGPIPE process kill | `MSG_NOSIGNAL` on every `send` |

Each row corresponds to at least one negative test in `tests/request_test.cpp`
or `tests/response_test.cpp`.

## Benchmarks

Full results and methodology: [docs/benchmarks.md](docs/benchmarks.md).

| Connections | Req/sec | p50 | p99 |
|---|---|---|---|
| 8 | 72,593 | 88us | 53.87ms |
| 100 | 75,803 | 65us | 371us |
| 500 | 77,101 | 72us | 252us |

Loopback, Release build, 8 workers. Throughput is flat because the server is
CPU-bound at 8 cores; tail latency improves with load because workers stop
stalling between requests.

## Known limitations

Each of these is deliberate, understood, and has a planned fix.

- **Idle keep-alive connections block the pool.** A worker stays blocked in
  `recv()` while a connection is idle, so N idle clients starve everyone
  else:

      for i in $(seq 1 8); do (nc 127.0.0.1 8080 &) ; done
      time curl -s http://127.0.0.1:8080/health     # 4.20s, vs ~80us normally

  Eight idle terminals made the server unresponsive for four seconds. A
  browser holds ~6 idle connections per origin, so two tabs get close to
  this. `SO_RCVTIMEO` bounds the damage; an `epoll` event loop removes the
  connection-to-thread coupling entirely.

- **No request bodies.** `POST` returns `405`. Requests that declare a body
  get a response and then a closed connection.

- **`SO_RCVTIMEO` resets per `recv`**, so it stops idle clients but not a
  Slowloris attacker trickling one byte at a time. A whole-request deadline
  needs an event loop with timers.

- **No TLS, no static file serving, no compression, no HTTP/2.**

## Development

```bash
./scripts/format.sh                              # apply clang-format
cmake --preset asan && ctest --preset asan       # memory errors
cmake --preset tsan && ctest --preset tsan       # data races
run-clang-tidy -p build -quiet \
  -header-filter='.*/cpp-http-server/(include|src)/.*' src/ include/
```

## Roadmap

- [x] RAII sockets, TCP echo server
- [x] Bounded thread pool (mutex + condition variable)
- [x] HTTP/1.1 parsing, routing, keep-alive
- [x] CI matrix with sanitizers and static analysis
- [ ] `epoll` event loop — non-blocking I/O, decoupling connections from threads
- [ ] Request bodies and `POST`
- [ ] Static file serving with MIME detection
- [ ] Graceful shutdown on `SIGTERM`

## License

MIT