# Benchmarks

**Machine:** <lscpu | grep 'Model name'>, 8 cores, 32 GB RAM, Kali Linux
**Build:** Release (`-O2`)
**Caveat:** all measurements are over loopback, with `wrk` running 4 threads
on the same 8-core machine as the server. Client and server compete for CPU,
and there is no real network latency. The *shape* of the results matters more
than the absolute numbers.

## Thread pool vs. epoll event loop

Same machine, same build, same commands.

`wrk -t4 -cN -d30s --latency http://127.0.0.1:8080/health`

| | Thread pool (8 threads) | epoll (1 thread) |
|---|---|---|
| Throughput @ 8 conns | 72,593 req/s | 68,753 req/s |
| Throughput @ 100 conns | 75,803 req/s | 74,719 req/s |
| Throughput @ 500 conns | 77,101 req/s | 65,813 req/s |
| p50 @ 8 conns | 88 us | 100 us |
| p50 @ 100 conns | 65 us | 1.27 ms |
| p50 @ 500 conns | 72 us | 7.36 ms |
| p99 @ 8 conns | 53.87 ms | 1.21 ms |
| p99 @ 500 conns | 252 us | 9.62 ms |
| Idle connections before the server stalls | 8 | none observed |

### Throughput barely changed going from 8 threads to 1

This was not the expected result. The explanation is that the workload is
**syscall-bound, not CPU-bound**: parsing a short request head and serialising
a few hundred bytes costs far less than the `recv`/`send`/`epoll_wait` round
trips around it. Dropping to one thread also removed 8 threads' worth of
context switching and cache-line contention, which offset most of the lost
parallelism.

### Latency shape changed substantially

A single event loop serves ready connections in sequence, so median latency
grows with concurrency: 100us at 8 connections, 7.36ms at 500. The thread
pool spread that work across 8 cores and kept p50 flat.

The tail tells the opposite story. At 8 connections the thread pool's
p99/p50 ratio was ~600x (88us vs 53.87ms) because threads blocked each other
unpredictably; the event loop's was ~12x (100us vs 1.21ms). **The event loop
is slower on average at high concurrency but far more predictable**, because
queuing is orderly rather than threads randomly contending.

## The idle-connection problem

This is what motivated the rewrite.

```
for i in $(seq 1 N); do (sleep 300 | nc 127.0.0.1 8080 >/dev/null 2>&1 &) ; done
sleep 2
ss -tn state established '( sport = :8080 )' | wc -l    # confirm they are open
time curl -s http://127.0.0.1:8080/health
```

| Server | Idle connections | Time for one request |
|---|---|---|
| Thread pool (8 workers) | 8 | **4.20 s** |
| epoll (1 thread) | 50 | **0.01 s** |

Under the thread pool, each idle keep-alive connection held a worker blocked
in `recv()`. Eight idle terminals made a server capable of 77,000 req/s take
over four seconds to answer one request; it only recovered when a connection
hit the 5-second `SO_RCVTIMEO`. The event loop has no such coupling: idle
connections cost a map entry, not a thread.

## Memory

| | Thread pool | epoll |
|---|---|---|
| Baseline RSS | — | 4,128 kB |
| RSS with 501 established idle connections | — | 4,128 kB |
| Reserved before serving anything | 8 MB stack × 8 threads = 64 MB | none |

With 501 connections confirmed established via `ss`, RSS was **unchanged from
baseline**. Each `Connection` holds a socket, a deadline, and two empty
`std::string`s — small enough that 500 of them fit inside heap pages the
allocator had already claimed, below the resolution of an RSS measurement
(which counts 4 KB pages).

The honest conclusion is not a precise per-connection figure but the
comparison: 500 idle connections were free at this resolution, while the
thread-pool design reserved 64 MB of stack before serving a single request.

## Reproducing

```bash
cmake --preset release
cmake --build --preset release
./build-release/server

# in another terminal
wrk -t4 -c8   -d30s --latency http://127.0.0.1:8080/health
wrk -t4 -c100 -d30s --latency http://127.0.0.1:8080/health
wrk -t4 -c500 -d30s --latency http://127.0.0.1:8080/health
```