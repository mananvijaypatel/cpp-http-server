# Benchmarks

**Machine:** <fill in: `lscpu | grep 'Model name'`>, 8 cores, 32 GB RAM, Kali Linux
**Build:** Release (`-O2`), 8 worker threads (one per core)
**Caveat:** all measurements are over loopback. There is no real network
latency, no TLS, and the client and server share the same CPU, so absolute
numbers are optimistic. The *shape* of the results is what matters.

## Throughput under sustained load

```
wrk -t4 -cN -d30s --latency http://127.0.0.1:8080/health
```

| Connections | Req/sec | p50 | p75 | p90 | p99 | Duration |
|---|---|---|---|---|---|---|
| 8   | 72,593 | 88us | 131us | 230us | 53.87ms | 12.2s * |
| 100 | 75,803 | 65us | 103us | 156us | 371us   | 21.6s * |
| 500 | 77,101 | 72us | 105us | 142us | 252us   | 30.1s   |

\* interrupted before the full 30s

**Throughput is flat, and tail latency *improves* with load.** Going from 8 to
500 connections raised throughput slightly (72.6k → 77.1k req/s) and dropped
p99 from 53.87ms to 252us.

This is the opposite of what a "8 workers means 8 concurrent clients" model
predicts. The reason is that a worker only blocks when a connection has
nothing to send. Under sustained load the next request is always already in
the kernel's receive buffer, so workers never idle and the server is simply
CPU-bound at 8 cores. Extra connections just keep the pipeline full, which
smooths out the stalls visible in the 8-connection run.

## The actual limit: idle keep-alive connections

The pool's ceiling is not throughput. It is the number of *simultaneously
open* connections, because a worker stays blocked in `recv()` for as long as
a connection is idle.

```
for i in $(seq 1 8); do (nc 127.0.0.1 8080 &) ; done
sleep 1
time curl -s http://127.0.0.1:8080/health
```

```
ok

real    4.20s
```

**4.20 seconds, against a normal ~80us.** Eight idle terminals made a server
that handles 77,000 requests per second unresponsive. The request only
completed when one idle connection hit the 5-second `SO_RCVTIMEO` and
released a worker.

A browser opens roughly six connections per origin and holds them open, so
two open tabs approach this condition on an 8-worker server.

### Mitigation and the real fix

`SO_RCVTIMEO` bounds the damage to the timeout value, but it does not remove
the coupling: connections still own threads. It also does not stop a
Slowloris-style client, since the timeout resets on every byte received.

An `epoll` event loop removes the coupling entirely. One thread can hold
thousands of idle connections at a cost of a few hundred bytes each, and
threads are spent only on connections that actually have data ready.

## Reproducing

```bash
cmake --preset release
cmake --build --preset release
./build-release/server
# then run the wrk commands above from another terminal
```