# How `performance_test`'s round-trip latency actually works

This explains, in detail, how the upstream Apex.AI `performance_test` measures
latency — so you understand exactly what our one-way method does and does not match,
and what we'll implement to add a round-trip path.

---

## The core problem round-trip solves: you only get ONE trustworthy clock

To measure one-way latency (send time on machine/process A, receive time on B) you
must subtract two timestamps taken on **different clocks**. Those clocks agree only
if they are:
- the same process/thread (our case — same `CLOCK_MONOTONIC` epoch), **or**
- synchronized to sub-microsecond accuracy (PTP/`phc2sys`) — rare and fragile.

Across two processes (let alone two machines) the monotonic clocks have **different,
unrelated epochs**, so `t_recv_on_B - t_send_on_A` is meaningless. `performance_test`
is designed to run pub and sub as **separate processes, possibly on separate
machines**, so it cannot rely on a shared clock. Round-trip sidesteps this entirely:
**both timestamps are taken on the SAME clock (the publisher's).**

---

## The mechanism: a timestamped message that comes back

`performance_test` runs two roles:

```
   ┌──────────── MAIN (publisher + latency calculator) ───────────┐
   │  t0 = now()                                                   │
   │  msg.id  = k                                                  │
   │  msg.time = t0          ── publish ──►                        │
   │                                          ┌──── RELAY ────┐    │
   │                                          │ on receive:   │    │
   │                                          │  echo msg back│    │
   │                                          │ (id+time      │    │
   │                                          │  unchanged)   │    │
   │                                          └──────┬────────┘    │
   │  ◄────────── receive echoed msg ───────────────┘             │
   │  t1 = now()                                                   │
   │  round_trip = t1 - t0     <- BOTH on MAIN's clock            │
   │  one_way   = round_trip / 2                                  │
   └──────────────────────────────────────────────────────────────┘
```

Key points:
- The **timestamp is created and consumed on the SAME node** (MAIN). No cross-clock
  subtraction — this is what makes it valid across processes/machines.
- The **relay** (a second `perf_test` instance launched with a "relay"/roundtrip
  role) does nothing but subscribe and immediately republish the *same* message on a
  return topic. It does not look at the timestamp; it just bounces it.
- Latency reported = **(t1 − t0) / 2**, i.e. round-trip halved, on the assumption the
  path is symmetric (forward ≈ return). For asymmetric paths this is an
  approximation — a known, documented limitation of the method.

## How it maps onto the code (conceptually)

In `performance_test` the data type carries two fields used for this:
- an **id / sequence** (to match an echo to its original, and detect loss/reorder),
- a **timestamp** field set at publish time.

The roles are selected at launch (e.g. `--roundtrip-mode Main` vs `--roundtrip-mode
Relay`):

- **Relay role**: its subscriber callback takes the received sample and publishes it
  straight back on the return topic — the id and timestamp ride along unchanged. It
  is a pure echo; it never computes latency.
- **Main role**: its publisher stamps `time = now()` and `id = k`, sends, and its
  subscriber callback (listening on the return topic) does, on each echoed sample:
  ```
  now = clock.now()
  rt  = now - sample.time          // sample.time was set by THIS node earlier
  latency_sample = rt / 2
  record(latency_sample)           // -> histogram / percentiles
  ```
  Because `sample.time` was written by Main and read by Main, only Main's clock is
  ever used.

When NO round-trip is requested (single-machine "publisher + subscriber in one
test"), `performance_test` can instead do a direct one-way measurement relying on a
shared/synced clock — but the **distributed, default-credible** path is round-trip.

## Loss, reorder, and "still in flight" handling
Because messages are tagged with an id, Main can:
- detect **lost** samples (ids that never came back within a window),
- detect **reordered** samples,
- avoid counting a sample twice.
`performance_test` reports these alongside latency, so a transport can't "win"
latency by silently dropping the slow frames.

## Statistics
Samples are accumulated into a latency distribution; `performance_test` reports
mean/min/max and percentiles, over a configurable measurement window, after a
warm-up — the same conventions our harness uses. The difference is purely **what one
latency sample IS**: for perf_test it's `round_trip/2` measured on one clock; for us
it's a direct one-way delta valid only because pub+sub share a process.

---

## So what does our harness do differently, and why it matters

| aspect | `performance_test` (round-trip) | our harness (one-way) |
|---|---|---|
| clocks used | ONE (Main's), via echo | ONE (shared in-process), direct |
| valid across processes? | **yes** | no (needs shared process/clock) |
| valid across machines? | **yes** | no |
| latency sample = | (t1 − t0) / 2 | t_recv − t_send |
| assumes path symmetric? | yes (fwd ≈ return) | n/a (direct) |
| extra cost included | the return hop + relay node | none (single hop, same proc) |

Implications for our numbers:
- Our one-way value is a **single forward hop with no return and no IPC** → it is
  **lower** than perf_test's round-trip/2 would be for the same transport.
- Our value is **only** valid because pub and sub share a process. The moment you
  move them to separate processes/machines, you MUST switch to round-trip (or a
  PTP-synced clock) — which is exactly why we'll add it.

---

## What we will implement to match it

A round-trip mode for each transport binary:
1. **Relay thread/process**: subscribe on topic `fwd`, republish the identical bytes
   (id + embedded timestamp untouched) on topic `ret`.
2. **Main**: publisher stamps `mono_ns()` + seq into the payload on `fwd`; a
   subscriber on `ret` computes `(mono_ns() - sent)` and divides by 2.
3. Because Main both stamps and reads, this stays valid even when Main and Relay are
   **separate processes** (each can use its own clock; only Main's is ever used for
   the delta) — unlocking the inter-process and (with care) cross-machine runs.
4. Keep the same percentile/warm-up/K-run/CPU/gate machinery; only the definition of
   "one latency sample" changes.

This gives us a second, literature-comparable latency column to sit beside the
current one-way column — and removes the "can't compare to published numbers"
caveat for that column.
