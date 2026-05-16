# LinkCheck / MAPE-K (firmware)

LoRaWAN link insight in **`mapek_link`**: observe (Monitor), classify (Analyze). Plan / Execute / Knowledge policy TBD — add here when we lock behavior.

## Monitor (M)

- **Role:** Collect telemetry only; no RF “verdict.”
- **Feeds (event-driven):** LinkCheckAns → margin + `#gw`; downlink → RSSI/SNR + app vs MAC + time-updated flag; LoRa thread → MCPS result (port/len/confirmed/errno class), LinkCheck TX outcomes, **join edges** (`feed_join`).
- **Storage:** RAM in `mapek_link.c` (mutex); EWMA on margin/gw and DL RSSI/SNR for smoothing.
- **Logs:** `mapek Mon:` on a timer (snapshot). Per-feed `mapek feed *` for bring-up visibility.

## Analyze (A)

- **Role:** Turn Monitor into a **small snapshot** for UI / later Plan.
- **RF vs session:** `link_state` / `rf` is **RF-only** (UNKNOWN → EXCELLENT…POOR). **`sess`** = MAC joined — **not** folded into RF grade.
- **How:** Build a **degradation score** from margin, gw, DL EWMA, stale LinkCheck-pending, recent confirmed RX-timeout; **EWMA-smooth** the score to reduce flapping; map smoothed score to `rf`.
- **Reasons:** `rsn` bitmask explains penalties (weak DL, low gw, no LC/DL sample, etc.).
- **Logs:** `mapek An:` on same timer pass as `mapek Mon:` (separate line). API: `mapek_link_analyze_get()`.

## Plan / Execute / Knowledge (P, E, K)

*Document policy here when defined: what to do given `rf` + `sess` + reasons (LinkCheck cadence, pacing, rejoin hints, NVM thresholds).*



You can do this with your current Monitor stage, but you need to stop thinking in terms of “raw MCPS events” and start treating your Monitor as a **connection inference engine**, not a logger.

Right now you already have enough signals. What you’re missing is a **single synthesized “gateway connectivity belief”** built from them.

---

# 1. First: define what “connected to a gateway” actually means

In LoRaWAN, “connected to gateway” is not a true session like TCP.

So you must redefine it as:

> “At least one gateway is currently receiving my uplinks and the network is able to deliver downlink (MAC or app).”

So connectivity = **bidirectional evidence**, not just join state.

---

# 2. What you already have in Monitor (good news)

From your logs, Monitor already gives you:

### Uplink success

* `UL OK`
* `mcps ret=0`

### Downlink presence

* RSSI/SNR from DL
* `DL app` or `DL MAC`

### MAC responsiveness

* LinkCheckReq / LinkCheckAns
* DevTimeReq / DevTimeAns
* ADR responses

### Failure signals

* `Rx 1/2 timeout`
* `MlmeConfirm failed`

This is already enough to infer connectivity.

---

# 3. The key idea: build a “Connectivity Score” in Monitor

Instead of waiting for Analyze, compute this inside Monitor:

```c
gateway_connectivity_score ∈ [0..100]
```

or even simpler:

```c
gateway_connected = true/false
```

But the score version is better.

---

# 4. How to compute it from YOUR existing signals

You already have everything needed.

## A. Positive evidence (increase score)

Every event adds points:

### Uplink success

```text
UL OK → +5
```

### Downlink received (any type)

```text
DL app or DL MAC → +15
```

### LinkCheck success

```text
LinkCheckAns → +20
```

### MAC ACK / confirmed success

```text
confirmed uplink success → +10
```

---

## B. Negative evidence (reduce score)

### RX timeouts

```text
MlmeConfirm failed → -15
```

### No DL observed for long time

```text
> 2 minutes → -10
> 5 minutes → -25
```

### LinkCheck pending too long

```text
LC_PENDING_STALE → -20
```

---

## C. Stabilization (VERY important)

Apply EWMA:

```c
score = (score * 0.8) + (instant_score * 0.2)
```

You already use EWMA elsewhere → reuse same idea.

---

# 5. Decision thresholds (THIS is your “connected or not” logic)

Now you define:

## Connected

```text
score >= 60
```

Meaning:

* uplinks are flowing
* at least occasional downlink exists
* MAC is responsive

---

## Degraded

```text
30 <= score < 60
```

Meaning:

* uplinks may work
* downlink unreliable
* gateway borderline

---

## Disconnected (gateway likely gone)

```text
score < 30
```

Meaning:

* no DL
* repeated RX failures
* LinkCheck failing or stale

---

# 6. What triggers “backoff mode”?

You said:

> “based on that it should set back off”

So define:

## Backoff condition

```text
score < 30 for N consecutive windows (e.g. 3 cycles)
```

OR stronger:

```text
3 consecutive LinkCheck failures
AND no DL in last X minutes
```

---

# 7. What Monitor should output (your new An line)

You already defined:

```
mapek An: rf=<u> sess=<u> d=<raw>/<smoothed> rsn=0x<hex>
```

Now extend conceptually (internally or in struct):

### Add derived field:

```text
gw_conn=<0|1>
gw_score=<0..100>
```

Example:

```
mapek An: rf=2 sess=1 d=75/19 rsn=0x00000006 gw_conn=0 gw_score=18
```

This makes everything downstream trivial.

---

# 8. How Analyze becomes simpler

Once Monitor gives score:

Analyze no longer asks:

* “are LinkChecks failing?”
* “are RSSI values bad?”

It just asks:

```text
gw_score trend down?
gw_conn false?
```

So Analyze becomes:

```c
if (gw_conn == 0 && gw_score < 30)
    state = DISCONNECTED;
```

That’s it.

---

# 9. Why your current signals are already enough

From your logs:

### You already detect:

* RX timeouts
* LinkCheck failure patterns
* RSSI degradation
* no DL periods
* confirmed uplink failures

So you are NOT missing data.

You are missing **aggregation**.

---

# 10. Minimal change architecture (important)

You do NOT need to redesign everything.

Just add:

## In Monitor stage:

### Step 1: event ingestion (you already have)

UL OK / DL / MCPS / LC / errors

### Step 2: update score

simple weighted update

### Step 3: output An struct

---

# 11. What NOT to do (common mistake)

Don’t do this:

❌ “If LinkCheck fails 3 times → disconnected”

Why wrong:

* LinkCheck depends on RX window timing
* can fail even when uplinks still work
* can be gateway timing issue, not loss

---

Instead:

✔ combine multiple signals:

* DL absence
* UL success/failure ratio
* LC behavior
* MAC timeouts

---

# 12. Final mental model (this is the key)

Think of Monitor as producing:

```
raw signals
   ↓
feature extraction
   ↓
connectivity score
   ↓
binary interpretation (Analyze)
   ↓
action (Plan)
```

---

# 13. If you implement only ONE thing

Implement this:

```c
gw_score = EWMA(
    uplink_success,
    downlink_seen,
    linkcheck_success,
    mac_timeouts,
    dl_freshness
);
```

Then:

```c
if (gw_score < threshold)
    trigger_backoff();
```

---

If you want, next step I can help you design:

* exact C struct for Monitor output
* scoring weights tuned for LoRaWAN (US915)
* or a state machine (CONNECTED / DEGRADED / LOST / REJOINING)

That’s where this becomes production-grade.
