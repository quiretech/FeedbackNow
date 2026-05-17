# MAPE-K link policy (firmware)

LoRaWAN **LNS reachability** in `mapek_link.c`: **Monitor → Analyze → Plan → Execute**, with **Knowledge** counters in RAM. MAPE-K does **not** call `lorawan_*` or clear `lora_joined_flag`; it queues **LinkCheck** and **session lost** via `lora_app` cmd queue. **`lora_thread`** owns OTAA, backoff, and rejoin.

Config: `app/include/sys_config.h` (`MAPEK_*`, `MAPEK_LAB_FAST_TEST`). API: `app/include/mapek_link.h`.

---

## Core definition

### “Heard by LNS” (primary metric)

Not MCPS uplink success alone. **Heard** = timestamp refreshed only when:

| Feed | Source |
|------|--------|
| `mapek_link_feed_dl` | Any downlink with RSSI (app, MAC, DeviceTime, etc.) |
| `mapek_link_feed_link_check` | LinkCheckAns (margin + gateway count) |

**Unconfirmed uplinks without downlink do not count as heard.** Joined MAC session can remain up while the LNS path is silent.

### LinkCheck role

LinkCheck is a **probe**, not the health metric. Plan queues forced LinkCheck when silence policy says the LNS path may be dead. **Housekeeping does not send LinkCheck** (HK: battery UL, counter sync, DeviceTime only).

### Boundaries (who does what)

| Layer | Responsibility |
|--------|----------------|
| **MAPE-K Monitor** | Facts: heard time/kind, DL EWMA, last LC sample, probe arm/TX/RX |
| **MAPE-K Analyze** | `link_state`, `probe_outcome`, `reasons` bitmask |
| **MAPE-K Knowledge** | `kn_plan_probe_fail_count`, plan cooldown timestamps, session-lost arm |
| **MAPE-K Plan** | Intent `PROBE_LC` → `lora_request_link_check(true)` (source **PLAN**) |
| **MAPE-K Execute** | After N **Plan** probe failures while STALE/DEGRADED → `lora_request_session_lost()` |
| **`lora_thread`** | OTAA join, uplink queue, `LORA_CMD_LINK_CHECK*`, session teardown, `LORA_JOIN_BACKOFF_HOURS`, silent rejoin |
| **HK** | Daily (or lab 120 s) SMF tick → battery, counters, DeviceTime — **no LC** |
| **SMF** | Modes/UI; receives `SMF_EVT_DISCONNECTED` on session lost (visibility only today) |

**Removed:** consecutive **confirmed UL** fail → rejoin in `lora_thread` (NFC/buttons/HK use unconfirmed in prod). Hard **`ENOTCONN`** on send still tears down session immediately.

**Post-join:** one LinkCheck probe (`MAPEK_PROBE_SOURCE_JOIN`) from `lora_thread` after OTAA; does not count toward session-lost Plan fail counter.

---

## Link state (`link_state`)

Evaluated each `mapek_link_step()` (end of LoRa thread loop iteration).

| State | Value | Condition (joined) |
|--------|-------|---------------------|
| `OK` | 0 | Heard age &lt; stale threshold; no active probe failure streak |
| `STALE` | 1 | Never heard, **or** `now - last_heard ≥ MAPEK_LNS_HEARD_STALE_MS` |
| `PROBE_PENDING` | 2 | Probe armed and outcome still `PENDING` |
| `DEGRADED` | 3 | `kn_probe_fail_count > 0` and recent terminal fail (NO_ANSWER / TX_FAIL) |

When **not joined**, Analyze reports `OK` (session layer is idle).

---

## Probe lifecycle

1. **BEGIN** (`mapek_link_feed_probe_begin`) — baseline `last_heard` before TX  
2. **TX** (`mapek_link_feed_probe_tx`) — `lorawan_request_link_check` return code  
3. **RX** — LC Ans and/or DL advances heard → SUCCESS  
4. **Timeout** — `MAPEK_PROBE_ANS_TIMEOUT_MS` with no heard advance → NO_ANSWER  
5. **TX_FAIL** — `probe_tx_ok == false` (e.g. MAC busy)

**Probe sources:** `JOIN` (post-join), `PLAN` (silence policy), `OTHER` (manual cmd queue LC only).

**Knowledge on terminal outcome:**

- **SUCCESS** → `kn_probe_fail_count = 0`, `kn_plan_probe_fail_count = 0`  
- **NO_ANSWER / TX_FAIL** → `kn_probe_fail_count++`; if source was **PLAN**, `kn_plan_probe_fail_count++`

Only **PLAN** failures count toward **session lost**.

---

## Plan policy

Runs after Analyze in `mapek_link_step()`. **No fixed periodic LC timer** (`MAPEK_PLAN_LINKCHECK_PERIOD_MS = 0`).

Queue **PROBE_LC** when **joined**, probe **not PENDING**, and cooldown elapsed:

| Trigger | Condition |
|---------|-----------|
| Never heard | `last_lns_heard_ms == 0` |
| STALE | Heard age ≥ `MAPEK_LNS_HEARD_STALE_MS` |
| DEGRADED | `link_state == DEGRADED` |

**Cooldown:**

- Normal: `MAPEK_PLAN_PROBE_COOLDOWN_MS` (30 min prod)  
- After `kn_probe_fail_count ≥ MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN` (3): `MAPEK_PLAN_PROBE_COOLDOWN_FAIL_MS` (2 h prod)

**Execute:** `mapek_link_tag_next_probe_source(PLAN)` + `lora_request_link_check(true)`.

---

## Execute (session lost)

When `MAPEK_SESSION_LOST_ENABLE` and:

- Joined, not already posted  
- Not `PROBE_PENDING`  
- `link_state` is **STALE** or **DEGRADED**  
- `kn_plan_probe_fail_count ≥ MAPEK_SESSION_LOST_PROBE_FAILS` (default **3**)

→ `lora_request_session_lost()` → `lora_thread`:

1. Clear `lora_joined_flag`, `mapek_link_feed_join(false)`  
2. `time_sync_abort_on_link_lost()`, `SMF_EVT_DISCONNECTED`  
3. Start backoff timer → `LORA_CMD_JOIN_SILENT` after `LORA_JOIN_BACKOFF_HOURS` (6 h prod; 1 min if `MAPEK_LAB_FAST_TEST`)

MAPE-K does **not** run OTAA itself.

---

## Production parameters (`MAPEK_LAB_FAST_TEST = 0`)

| Define | Value | Role |
|--------|-------|------|
| `MAPEK_LNS_HEARD_STALE_MS` | 6 h | No heard → STALE → Plan may probe |
| `MAPEK_PROBE_ANS_TIMEOUT_MS` | 2 min | Probe → NO_ANSWER |
| `MAPEK_PLAN_PROBE_COOLDOWN_MS` | 30 min | Min gap between Plan LCs |
| `MAPEK_PLAN_PROBE_COOLDOWN_FAIL_MS` | 2 h | After ≥3 probe fails |
| `MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN` | 3 | Long cooldown threshold |
| `MAPEK_SESSION_LOST_PROBE_FAILS` | 3 | Plan fails → session lost |
| `MAPEK_PLAN_LINKCHECK_PERIOD_MS` | 0 | Silence policy only |
| `LORA_JOIN_BACKOFF_HOURS` | 1 h | Before silent rejoin |
| `LORA_NFC_UPLINK_CONFIRMED` | 0 | No confirmed-fail rejoin path |
| `HEARTBEAT_USE_DEVEUI_JITTER` | 1 | HK ~daily UTC + DevEUI offset |

Install UI margin/gw: `lora_link_stats` (not MAPE-K snapshot).

---

## Lab test (`MAPEK_LAB_FAST_TEST = 1`)

Set `#define MAPEK_LAB_FAST_TEST 1` in `sys_config.h`, rebuild. Overrides:

| Define | Lab |
|--------|-----|
| `MAPEK_LNS_HEARD_STALE_MS` | 1 min |
| `MAPEK_PROBE_ANS_TIMEOUT_MS` | 30 s |
| `MAPEK_PLAN_PROBE_COOLDOWN_MS` | 45 s |
| `LORA_JOIN_BACKOFF_HOURS` | 0 → 1 min rejoin |
| `HEARTBEAT_USE_DEVEUI_JITTER` | 0 → HK every 120 s |

**Ship with `MAPEK_LAB_FAST_TEST 0`.**

---

## Logging (event-only)

One format; **no periodic Mon/An**, no per-feed spam.

```text
mapek: ev <event> j=<0|1> st=<OK|STALE|PENDING|DEGRADED> heard=<Ns|never> pf=<plan_fails>/3 probe=<idle|PENDING|OK|NO_ANSWER|TX_FAIL>
```

| Event | When |
|--------|------|
| `init` | Boot (`mapek_link_init`) |
| `join=0` / `join=1` | Session edge |
| `probe OK\|NO_ANSWER\|TX_FAIL src=JOIN\|PLAN\|OTHER` | Terminal probe outcome |
| `st OK->STALE` (etc.) | `link_state` change |
| `plan LC cd=…s` | Plan queued LinkCheck |
| `session_lost` | Execute (WRN) |

Routine votes, UL OK, and DL do **not** log unless they change MAPE-K state.

---

## Assumptions and limits

1. **Gateway down ≠ always `ENOTCONN`** — unconfirmed UL may still return success; MAPE-K detects silence via **heard age**, not UL ACK.  
2. **STALE is not instant** — by design (~6 h prod) to avoid flapping on quiet devices.  
3. **Three Plan failures before OTAA** — spotty LTE can recover on probe 1–2 without rejoin if LC/DL returns (SUCCESS resets `pf`).  
4. **Join probe / HK / DeviceTime** do not increment `kn_plan_probe_fail_count`.  
5. **RF tier** (`MAPEK_RF_*`, DL EWMA) fills `reasons` bits only; **STALE uses heard age**, not RSSI.  
6. **`mapek_link_step`** runs on LoRa thread loop; feeds from DL callback may run before next step.  
7. **Rejoin** after session lost is always full OTAA cycle (new DevAddr possible); not ADR/link reset alone.

---

## Sample flow: gateway / LNS path dies abruptly (production timings)

**Setup:** Device joined, heard recently (DL or LinkCheckAns). Gateway backhaul or NS stops answering; MAC join remains.

| Phase | Time (order of magnitude) | What happens |
|-------|---------------------------|--------------|
| 1. Normal | T+0 | Votes/HK UL may succeed; occasional DL refreshes heard → `st=OK`, no mapek lines |
| 2. Silence | T+0 … ~6 h | `last_lns_heard` frozen; ULs may still log `UL OK`; **no** mapek unless state changes |
| 3. STALE | ~6 h | Analyze: `st OK→STALE` → `mapek: ev st OK->STALE heard=…` |
| 4. Plan LC #1 | Soon after | `mapek: ev plan LC cd=1800s` → forced LinkCheck; ~2 min → `probe NO_ANSWER src=PLAN pf=1/3`; likely `DEGRADED` |
| 5. Plan LC #2 | +30 min cooldown | Second NO_ANSWER → `pf=2/3` |
| 6. Plan LC #3 | +30 min | Third NO_ANSWER → `pf=3/3` |
| 7. Session lost | Immediately after 3rd fail | `mapek: ev session_lost` (WRN) → `LoRa session lost (mapek)` → `join=0` |
| 8. Backoff | +1 h | Still not joined; UL queue may fail |
| 9. Rejoin | After backoff | `join backoff expired; rejoin (silent)` → OTAA cycle (20×30 s attempts) |
| 10. Recovery | If GW back | `probe OK src=JOIN`, `join=1`, heard fresh → `st=OK` |

**Rough prod timeline:** ~6 h to first Plan LC, ~1 h more for three Plan probes (30 min cooldowns), +1 h backoff ≈ **~8 h** to rejoin attempt unless path recovers earlier.

---

## Sample flow: spotty LTE (gateway returns before session lost)

Same as above through **Plan LC #1** → `probe NO_ANSWER`, `pf=1/3`.

Before **Plan LC #2** (~30 min later), gateway/LNS path returns:

- Next Plan LC or any DL → `probe OK src=PLAN` (or heard via DL)  
- `pf=0/3`, `st=OK`  
- **No** `session_lost`; **no** OTAA; session continues  

This is the intended “fast recovery without rejoin” path.

---

## Sample flow: hard MAC session loss

If `lorawan_send` returns **`-ENOTCONN`**:

- `lora_thread` calls `lora_session_lost_teardown("ENOTCONN")` immediately (no wait for 3 Plan fails)  
- Same backoff → silent rejoin as MAPE-K Execute  

---

## Code map

| File | Role |
|------|------|
| `app/src/mapek_link.c` | MAPE-K implementation |
| `app/include/mapek_link.h` | Types, feeds, snapshots |
| `app/src/lora_thread.c` | Join, UL, LC cmd, `lora_session_lost_teardown`, post-join probe |
| `app/src/lora_app.c` | `lora_request_link_check`, `lora_request_session_lost` |
| `app/src/lora_link_stats.c` | LC Ans → install stats + `mapek_link_feed_link_check` |
| `app/src/lora_app.c` (DL path) | `mapek_link_feed_dl` |
| `app/src/housekeeping.c` | HK without LC |

---

## `mapek_link_step()` order

```text
analyze_run_locked()   → probe outcome, link_state, event logs (st/probe)
plan_execute_locked()  → maybe queue Plan LC
execute_session_lost_locked() → maybe queue SESSION_LOST
```

Called from `lora_thread` message loop after cmd/uplink handling.
