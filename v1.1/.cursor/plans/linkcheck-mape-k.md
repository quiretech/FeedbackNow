# MAPE-K link policy (firmware v2)

LoRaWAN **LNS reachability** via a modular **MAPE-K** stack: **Monitor → Analyze → Plan → Execute**, with **Knowledge** in RAM. A dedicated **coordinator thread** runs the loop every 5 s. MAPE-K does **not** call `lorawan_*` or clear `lora_joined_flag`; **Execute** posts `lora_request_time_sync()`, `lora_request_link_check()`, and `lora_request_session_lost()` through `lora_app` cmd queue. **`lora_thread`** owns OTAA, uplink queue, and session teardown.

**Public API:** `app/include/mapek_coordinator.h`  
**Config:** `app/include/mapek/mapek_config.h` (+ RF thresholds from `sys_config.h`)  
**Lab flag:** `MAPEK_LAB_FAST_TEST` in `sys_config.h`

---

## Architecture

```text
                    ┌─────────────────────────────────────┐
                    │  mapek_coordinator (5 s thread)      │
                    │  snapshot → analyze → plan → execute │
                    └─────────────────────────────────────┘
           feeds ▲                    │ lora_request_*
                  │                    ▼
    lora_thread ──┼── mapek_mon.c     lora_app cmd queue → lora_thread
    lora_app DL ──┘    mapek_knowledge.c
    link_stats LC ──── mapek_analyze.c  (pure: F2/F3 scores, hysteresis, urgency)
                       mapek_plan.c     (pure: HB schedule, rejoin intents)
                       mapek_execute.c  (side effects only)
```

### Dependency rules

| Module | May include |
|--------|-------------|
| `mapek_types.h` | stdint, stdbool only |
| `mapek_config.h` | sys_config.h |
| `mapek_knowledge` | types |
| `mapek_mon` | types |
| `mapek_analyze` | types, knowledge (read-only in `plan_run`) |
| `mapek_plan` | types, knowledge |
| `mapek_execute` | types, knowledge, lora_app |
| `mapek_coordinator` | all phases + Zephyr |

Phases do **not** include each other's headers (no circular deps).

### Inter-phase contracts (`mapek_types.h`)

| Struct | Producer | Consumer |
|--------|----------|----------|
| `mon_snap_t` | Monitor | Analyze |
| `analyze_out_t` | Analyze | Plan, Execute, coordinator |
| `plan_out_t` | Plan | Execute |

`analyze_out_t` includes **`hypothesis`**, **`confidence`** (0–100), **`urgency`** (0–100), **`f2_score`**, **`f3_score`**, plus `link_state` and `uplink_allowed`.

---

## What was removed (do not re-introduce)

| Removed | Replacement |
|---------|-------------|
| Probe armed/disarmed state machine | Daily/6h **heartbeat** (DeviceTimeReq + LinkCheckReq) |
| `MAPEK_PROBE_SOURCE_*`, `MAPEK_PROBE_OUTCOME_*` | `heartbeat_result_t` |
| `MAPEK_LINK_STATE_PROBE_PENDING` | Internal HB `PENDING`, not a link state |
| `mapek_link_feed_probe_begin/tx/armed()` | `mapek_feed_heartbeat_tx()` |
| `mapek_link_step()` in `lora_thread` | Coordinator thread |
| `mapek_link.c` / `mapek_link.h` monolith | `app/src/mapek/*.c` |
| Plan-driven LinkCheck on STALE cooldown | HB cadence from Knowledge |
| `kn_plan_probe_fail_count` → 3 LC → session lost | F2 wait → F3 hypothesis → urgency-gated OTAA |

---

## Failure taxonomy (F1–F4)

```text
Device ──RF──▶ RAK GW ──4G──▶ AWS IoT Core ──▶ Backend
        [F1]      [F2]    [F3]      [F4]
```

| Class | Hypothesis | Response |
|-------|------------|----------|
| **F1** RF | `HYPOTHESIS_RF_ISSUE` | Wait. Do not rejoin. |
| **F2** Gateway 4G backhaul | `HYPOTHESIS_BACKHAUL_OUTAGE` | Wait 4 h (prod). HB every 6 h when degraded. |
| **F3** LNS session corrupt | `HYPOTHESIS_SESSION_CORRUPT` | OTAA rejoin with jitter after backhaul wait + hysteresis. |
| **F4** Device deleted from LNS | `HYPOTHESIS_DELETED` | Long backoff (6 h prod), keep retrying forever. |

**F2 vs F3** is **not** a hard 4 h boundary. Analyze computes:

- `f2_score = backhaul_likelihood(time, RF, HB pattern)`
- `f3_score = session_likelihood(time, OTAA history)`
- Pick hypothesis from scores (F4 when `otaa_cycle_count ≥ MAPEK_OTAA_MAX_CYCLES`).

**Diagnosis vs action:** `hypothesis` + `confidence` + `urgency` → Plan. OTAA only when `urgency ≥ MAPEK_URGENCY_REJOIN_MIN` (and backoff elapsed).

---

## “Heard by LNS” (Monitor)

**Heard** = `last_lns_heard_ms` refreshed on:

| Feed | API |
|------|-----|
| Any DL (app, MAC, DeviceTime) | `mapek_feed_dl(rssi, snr, feed_flags)` |
| LinkCheckAns | `mapek_feed_link_check_ans(margin_db, nb_gw)` |

Unconfirmed uplinks without downlink do **not** count as heard.

---

## Heartbeat = sole probe

One scheduled frame per cycle: **DeviceTimeReq + LinkCheckReq** (Execute fires `lora_request_time_sync()` + `lora_request_link_check(false)`).

### `heartbeat_result_t`

| Value | Meaning |
|-------|---------|
| `HB_NOT_FIRED` | No HB attempted yet |
| `HB_PENDING` | TX queued; waiting for DL/LC Ans |
| `HB_SUCCESS` | LinkCheckAns or any DL after HB TX |
| `HB_NO_ANSWER` | `MAPEK_HB_ANS_TIMEOUT_MS` elapsed with no DL |
| `HB_TX_FAIL` | Link check / MAC TX failed |

Coordinator promotes `PENDING → NO_ANSWER` on timeout. Feeds: `mapek_feed_heartbeat_tx(tx_ret)` from `lora_thread` after LC cmd.

### Cadence (`mapek_knowledge`)

| Mode | Interval | Jitter |
|------|----------|--------|
| Normal (last HB OK) | `MAPEK_DAILY_HB_INTERVAL_MS` (24 h prod) | `MAPEK_DAILY_HB_JITTER_MS` (±1 h, DevEUI seed) |
| Degraded (last HB NO_ANSWER) | `MAPEK_STALE_HB_CADENCE_MS` (6 h prod) | `MAPEK_STALE_HB_JITTER_MS` (±30 min) |

`next_hb_due_ms` updated in Knowledge when HB completes (not when fired).

**Housekeeping** still runs battery/counter/DeviceTime on its own schedule; MAPE-K HB is the **link-health** probe. Avoid duplicating lab expectations with HK every 120 s when testing MAPE-K timing.

---

## Link states

| State | Uplink (`mapek_uplink_allowed`) | Notes |
|-------|-----------------------------------|--------|
| `LINK_OK` | yes | Normal |
| `LINK_STALE` | yes | Optimistic; `lns_heard_age ≥ MAPEK_STALE_THRESHOLD_MS` |
| `LINK_DEGRADED` | **no** | After N consecutive HB failures; EEPROM counters still increment |
| `LINK_SUSPECT_SESSION` | no | F3 + hysteresis + urgency; Plan may OTAA |
| `LINK_SUSPECT_DELETED` | no | F4; slow OTAA retry |

### Hysteresis (Analyze + Knowledge)

- **`MAPEK_HB_FAIL_STREAK_DEGRADED`** (2): consecutive HB NO_ANSWER/TX_FAIL before STALE→DEGRADED.
- **`MAPEK_DEGRADED_TO_SUSPECT_MS`**: extra time in DEGRADED on top of `MAPEK_BACKHAUL_WAIT_MS` before SUSPECT_SESSION.
- **`committed_link_state`** in KB: dampens oscillation; Analyze proposes, KB commits on change.

When not joined, Analyze reports `LINK_OK`.

---

## Coordinator loop (every `MAPEK_STEP_MS` = 5 s)

```text
1. mapek_mon_snapshot()
2. mapek_knowledge_get()
3. HB timeout: PENDING + age ≥ MAPEK_HB_ANS_TIMEOUT_MS → feed HB_NO_ANSWER
4. HB terminal edge → update streaks, schedule next_hb_due_ms
5. mapek_analyze_run(mon, kb, &ana)
6. kb_commit_state (degraded_since_ms, etc.)
7. mapek_plan_run(ana, kb, now, &plan)
8. mapek_execute_run(plan, ana, kb, now)
9. if INTENT_FIRE_HEARTBEAT → mapek_feed_heartbeat_tx(0)
10. atomic_set(mapek_uplink_allowed_flag, ana.uplink_allowed)
```

**Not** called from `lora_thread` message loop anymore.

---

## Plan intents

| Intent | When | Execute |
|--------|------|---------|
| `INTENT_NONE` | Default | — |
| `INTENT_FIRE_HEARTBEAT` | `now ≥ next_hb_due_ms`, joined | time sync + link check |
| `INTENT_REJOIN` | `SUSPECT_SESSION`, F3, urgency OK, OTAA backoff | jitter sleep → `lora_request_session_lost()` |
| `INTENT_REJOIN_SLOW` | `SUSPECT_DELETED`, F4, deleted retry elapsed | long jitter → session lost |

Rejoin overrides HB when both are due.

---

## Production parameters (`MAPEK_LAB_FAST_TEST = 0`)

| Define | Value | Role |
|--------|-------|------|
| `MAPEK_STALE_THRESHOLD_MS` | 2 h | Secondary guard → STALE |
| `MAPEK_HB_ANS_TIMEOUT_MS` | 90 s | HB PENDING → NO_ANSWER |
| `MAPEK_DAILY_HB_INTERVAL_MS` | 24 h | Normal HB period |
| `MAPEK_STALE_HB_CADENCE_MS` | 6 h | HB when degraded |
| `MAPEK_BACKHAUL_WAIT_MS` | 4 h | F2 primary window |
| `MAPEK_DEGRADED_TO_SUSPECT_MS` | 30 min | Extra DEGRADED before SUSPECT |
| `MAPEK_HB_FAIL_STREAK_DEGRADED` | 2 | HB failures → DEGRADED |
| `MAPEK_OTAA_MAX_CYCLES` | 3 | → F4 hypothesis |
| `MAPEK_OTAA_CYCLE_BACKOFF_MS` | 2 h | Between OTAA attempts |
| `MAPEK_DELETED_RETRY_MS` | 6 h | F4 retry period |
| `MAPEK_URGENCY_REJOIN_MIN` | 55 | Min urgency for OTAA |
| `LORA_JOIN_BACKOFF_HOURS` | 1 h | After session lost teardown |

Legacy `MAPEK_*` probe defines in `sys_config.h` are **unused** by v2; timing is in `mapek_config.h`.

---

## Lab test (`MAPEK_LAB_FAST_TEST = 1`)

| Define | Lab |
|--------|-----|
| `MAPEK_STALE_THRESHOLD_MS` | 2 min |
| `MAPEK_HB_ANS_TIMEOUT_MS` | 30 s |
| `MAPEK_DAILY_HB_INTERVAL_MS` | 5 min |
| `MAPEK_STALE_HB_CADENCE_MS` | 3 min |
| `MAPEK_BACKHAUL_WAIT_MS` | 5 min |
| `MAPEK_DEGRADED_TO_SUSPECT_MS` | 2 min |
| `MAPEK_DELETED_RETRY_MS` | 10 min |
| `HEARTBEAT_USE_DEVEUI_JITTER` | 0 → HK every 120 s |

**Ship with `MAPEK_LAB_FAST_TEST 0`.**

---

## Application integration

```c
// Button path (app_logic.c) — EEPROM counter always incremented in payload_gen
if (lora_is_joined() && mapek_uplink_allowed()) {
    lora_put_event(&msg, ...);
}
```

Backend reconstructs missed presses from **FCnt gaps + RTC** in payload; no on-device replay buffer.

---

## Logging (event-only)

```text
mapek: ev=<event> j=<0|1> st=<OK|STALE|DEGRADED|SUSPECT|DELETED> hyp=<RF|F2|F3|F4|?>
     conf=<0-100> urg=<0-100> heard=<Ns|never> hb=<idle|PENDING|OK|NO_ANSWER|TX_FAIL>
     f2=<score> f3=<score> otaa=<n>
```

| Event | When |
|--------|------|
| `init` | `mapek_init()` |
| `st X->Y` | Committed link state change |
| `hyp` | Hypothesis change |
| `hb` | Terminal heartbeat result |
| `otaa` | Rejoin intent |

---

## Sample flow: gateway 4G drops (production)

| Phase | Time | What happens |
|-------|------|----------------|
| 1 | T+0 | Last HB OK; button uplinks allowed |
| 2 | T+2 h | `lns_heard_age > STALE_THRESHOLD` → **STALE** (uplinks still allowed) |
| 3 | ~T+24 h | Daily HB fires → TX OK, no LC Ans → **DEGRADED** (uplinks suppressed) |
| 4 | T+24 h+4 h | `BACKHAUL_WAIT` exceeded; f3 > f2 → **SUSPECT_SESSION** (after hysteresis + urgency) |
| 5 | | OTAA with jitter; if GW still down, JoinAccept may not arrive → `otaa_cycle_count++` |
| 6 | After 3 cycles | **SUSPECT_DELETED** → 6 h slow retry |
| Recovery | Any time | HB SUCCESS or DL → **OK**, `mapek_uplink_allowed` true, flush suppression |

Rough prod: STALE at 2 h, first degrading HB ~24 h, OTAA earliest ~28 h+ (not the old ~8 h / 3× Plan LC path).

---

## Sample flow: spotty LTE recovers during F2 window

HB NO_ANSWER → DEGRADED, but `degraded_ms < BACKHAUL_WAIT` and **f2_score > f3_score** → stay DEGRADED, no OTAA. Next HB SUCCESS → OK, `hb_fail_streak = 0`, normal cadence. No session lost.

---

## Hard MAC session loss

`lorawan_send` **`-ENOTCONN`** → `lora_session_lost_teardown()` immediately (no MAPE-K wait). Same backoff → silent rejoin as Execute path.

---

## Code map

| File | Role |
|------|------|
| `app/include/mapek/mapek_types.h` | Enums, structs |
| `app/include/mapek/mapek_config.h` | LAB / PROD timeouts |
| `app/include/mapek/mapek_knowledge.h` | Shared KB |
| `app/include/mapek/mapek_mon.h` | Feed API |
| `app/include/mapek/mapek_analyze.h` | Pure analyze |
| `app/include/mapek/mapek_plan.h` | Pure plan |
| `app/include/mapek/mapek_execute.h` | Side effects |
| `app/include/mapek_coordinator.h` | Public API, uplink gate |
| `app/src/mapek/mapek_*.c` | Implementations |
| `app/src/lora_thread.c` | Join, UL, LC cmd, `mapek_feed_*` (no `mapek_link_step`) |
| `app/src/lora_app.c` | DL → `mapek_feed_dl`, init/start coordinator |
| `app/src/lora_link_stats.c` | LC Ans → `mapek_feed_link_check_ans` |
| `app/src/app_logic.c` | `mapek_uplink_allowed()` gate |
| `app/src/housekeeping.c` | Battery/counters/HK (no MAPE-K LC) |

---

## Assumptions and limits

1. **Gateway down ≠ `ENOTCONN`** — silence detected via heard age + HB, not UL ACK alone.  
2. **STALE is optimistic** — uplinks still sent until HB proves path dead.  
3. **Probabilistic F2/F3** — avoids cliff at exactly 4 h; urgency prevents premature OTAA.  
4. **Coordinator 5 s tick** — HB timeout resolution ±5 s.  
5. **Rejoin** is full OTAA after session lost teardown, not ADR-only reset.  
6. **Install UI** link stats still from `lora_link_stats`, not MAPE-K snapshot API.
