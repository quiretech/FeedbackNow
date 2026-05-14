# System status detection for LNS
_Exported on 5/10/2026 at 23:48:04 EDT from Cursor (3.3.30)_

---

**User**

today, how does our system learns that it is currently joined a lns or not.

---

**Cursor**

Here's how the system today learns its "currently joined to LNS" state. There are two distinct concepts in the code — make sure not to confuse them.

## 1. Runtime "currently joined" state — `lora_joined_flag`

This is an in-RAM atomic flag, not persisted:

```22:24:app/src/lora_app.c
/* Join status tracking */
atomic_t lora_joined_flag = ATOMIC_INIT(0);
K_SEM_DEFINE(lora_join_sem, 0, 1);
```

Everyone in the codebase queries it through the inline `lora_is_joined()`:

```141:147:app/include/lora_app.h
/**
 * @brief Check if device has successfully joined the LoRaWAN network
 * @return true if joined, false otherwise
 */
static inline bool lora_is_joined(void) {
  return atomic_get(&lora_joined_flag) != 0;
}
```

It's used by `housekeeping.c`, `app_logic.c`, `time_sync.c`, `downlink_dispatch.c`, `lora_app.c::lora_put_event`, and `smf_system_mode.c` to gate any work that requires the link.

### How that flag gets set / cleared

It is only ever written from `lora_thread.c`, in three places:

1. **Set to 1** after `lorawan_join()` returns 0 AND the post-join MAC probe succeeds:

```286:291:app/src/lora_thread.c
LOG_INF("LoRaWAN joined");
consecutive_send_failures = 0;
k_timer_stop(&join_after_backoff_timer);
atomic_set(&lora_joined_flag, 1);
lora_on_join_success();
k_sem_give(&lora_join_sem);
```

2. **Cleared on `-ENOTCONN`** returned by `lorawan_send()` — i.e. the MAC itself told us we're not on a network:

```418:428:app/src/lora_thread.c
if (ret == -ENOTCONN) {
  if (lora_is_joined()) {
    atomic_set(&lora_joined_flag, 0);
    time_sync_abort_on_link_lost();
    lora_reset_dr_time_sync_retry();
    (void)smf_post_event(SMF_EVT_DISCONNECTED, 0, k_uptime_get());
    k_timeout_t backoff = LORA_JOIN_BACKOFF_HOURS > 0
                              ? K_HOURS(LORA_JOIN_BACKOFF_HOURS)
                              : K_MINUTES(1);
    k_timer_start(&join_after_backoff_timer, backoff, K_NO_WAIT);
    LOG_INF("ENOTCONN: cleared joined; rejoin after backoff");
  }
}
```

3. **Cleared after N consecutive confirmed-uplink failures** (the soft "link lost" heuristic — typically Rx2 timeouts on confirmed frames):

```433:452:app/src/lora_thread.c
if (msg.confirmed) {
  consecutive_send_failures++;
  if (LORA_SEND_FAILURES_BEFORE_BACKOFF > 0 &&
      consecutive_send_failures >=
          (uint32_t)LORA_SEND_FAILURES_BEFORE_BACKOFF &&
      lora_is_joined()) {
    LOG_WRN(
        "Link lost: %u consecutive confirmed send failures; "
        "clearing joined, scheduling re-join after backoff",
        consecutive_send_failures);
    atomic_set(&lora_joined_flag, 0);
    time_sync_abort_on_link_lost();
    lora_reset_dr_time_sync_retry();
    consecutive_send_failures = 0;
    (void)smf_post_event(SMF_EVT_DISCONNECTED, 0, k_uptime_get());
    ...
```

So the runtime "am I joined right now?" answer is essentially:
- **TX side feedback only.** We never get an event from the stack saying "you're disconnected." We learn it the next time we *try* to transmit and either:
  - the MAC immediately rejects with `-ENOTCONN`, or
  - we've burned `LORA_SEND_FAILURES_BEFORE_BACKOFF` confirmed frames in a row with no ACK.

Unconfirmed sends never trigger the heuristic (no ACK is expected, so a timeout is benign — comment at `lora_thread.c:431`).

## 2. Persisted "have we ever joined before?" — `join_state_store`

Separate concern. This is what `join_state_store.c` manages — a single byte plus magic in EEPROM at `EEPROM_JOIN_STATE_OFF`:

```26:30:app/src/join_state_store.c
struct __packed join_state_record {
  uint32_t magic;
  uint8_t has_joined_once;
  uint8_t pad[EEPROM_JOIN_STATE_SIZE - 4U - 1U];
};
```

It is written once, the first time we ever succeed an OTAA join, from `run_join_cycle`:

```303:306:app/src/lora_thread.c
(void)smf_post_event(SMF_EVT_JOINED, show_join_led ? 1u : 0u,
                     k_uptime_get());
(void)join_state_store_set_has_joined_once();
rail_manager_release_3v3a();
```

It is read once at boot by the LoRa thread to decide its startup flow — first boot waits for the installer's staff combo, every subsequent boot silently auto-posts a join:

```356:368:app/src/lora_thread.c
bool has_joined_once = false;
(void)join_state_store_has_joined_once(&has_joined_once);
LOG_INF("has_joined_once=%d clear_on_boot=%d", has_joined_once,
        EEPROM_JOIN_STATE_CLEAR_ON_BOOT);
if (has_joined_once) {
  /* One join cycle with LED after power-on so installers see progress without
   * logs; backoff rejoin clears this flag before posting JOIN_SILENT. */
  (void)atomic_set(&silent_join_installer_led_armed, 1);
  (void)lora_cmd_put(
      LORA_CMD_JOIN_SILENT); /* Auto-join: no Connecting screen */
} else {
  LOG_INF("first boot: wait staff combo join");
}
```

Crucially, **this EEPROM bit is never read to decide "am I joined now?"** It only controls boot-time behavior (silent rejoin vs. wait for installer). After reset, `lora_joined_flag` always starts at 0 and we have to re-OTAA before any code path believes we're on the network.

## Summary

| Question | Source of truth | Lifetime |
|---|---|---|
| Am I joined to an LNS *right now*? | `lora_joined_flag` atomic, queried via `lora_is_joined()` | RAM only; resets to 0 on every boot |
| Have I ever successfully joined this LNS? | `has_joined_once` byte in EEPROM (`join_state_store`) | Persisted across reboots |

And the only signals we use to *learn* the runtime answer are:
- **Joined → true:** `lorawan_join()` returns 0 and a post-join `LinkCheckReq` probe succeeds.
- **Joined → false:** an uplink returns `-ENOTCONN`, or `LORA_SEND_FAILURES_BEFORE_BACKOFF` consecutive *confirmed* uplinks fail.

There is no passive/periodic "is the LNS still there?" check — we infer link state purely from outbound send results.

---

**User**

ok what should we chnge and improve to get a better system that knows that its connected to lns more hollistaclly, it doesnt need to know all the time, thats why were in unconfirmed, suggestions? what should we change in our system or remove, 

---

**Cursor**

Good question — and you've already hit the key constraint: most traffic is unconfirmed, so the system has very few hard signals to learn from, *and* it doesn't actually need a low-latency answer. That means the right model is **evidence-based, soft, and lazy** instead of the current **TX-failure-reactive** model. Here's what I'd change.

## Mental model shift

Today the system conflates two things into one flag:

- **Session joined?** — Do we have valid OTAA session keys?
- **Link reachable?** — Has the LNS recently shown any sign of life?

Split them, and treat "link reachable" as a soft, time-decaying belief that you only re-evaluate when something asks (housekeeping, install screen, etc.), not on every TX.

```
DISCONNECTED  ── lorawan_join() ──>  JOINED_HEALTHY  ── time without evidence ─>  JOINED_STALE
       ^                                  ^                                            │
       │                                  │ any positive evidence                      │
       │                                  └────────────────────────────────────────────┘
       │                                                                                │
       └── -ENOTCONN, or STALE + probe failed ──────────────────────────────────────────┘
```

`lora_is_joined()` stays as the fast atomic accessor; what *changes* is when and how it gets cleared.

## Harvest positive evidence you're already throwing away

Right now we only react to *failure*. We have several "the LNS clearly heard us" events that we don't feed back into the state:

| Event | Where it fires today | What we do with it |
|---|---|---|
| Any downlink (app port) | `lora_app_dl_callback` | Forward to SMF, log |
| MAC-only RX | same callback, `frmpayload==0` | Just log |
| `LinkCheckAns` | `lora_link_stats` callback | Cache margin/gw count; never feed back |
| `DeviceTimeAns` | `LORAWAN_TIME_UPDATED` flag | Update RTC |
| `LinkADRReq` causing DR change | `lora_app_dr_changed` | Set `latest_dr_seen` |
| Confirmed UL ACK | `lorawan_send` returns 0 with confirmed | Reset `consecutive_send_failures` |

**Add one tiny module** — call it `lora_link_health` — that exposes:

```c
void lora_link_note_lns_evidence(enum lns_evidence src);  // call from each hook above
uint32_t lora_link_seconds_since_lns_evidence(void);
enum link_state lora_link_state(void);   // HEALTHY / STALE / SUSPECT / LOST
```

Internally it just stores a single `atomic_t last_evidence_uptime_s` plus a tiny EWMA of confirmed-uplink success. The hooks are one-liners in callbacks you already have.

This gives you a **passive, free** read on "is the LNS still talking to us?" without sending anything.

## Replace the streak counter with a windowed/EWMA score

```435:452:app/src/lora_thread.c
if (msg.confirmed) {
  consecutive_send_failures++;
  if (LORA_SEND_FAILURES_BEFORE_BACKOFF > 0 &&
      consecutive_send_failures >=
          (uint32_t)LORA_SEND_FAILURES_BEFORE_BACKOFF &&
      lora_is_joined()) {
    ...
    atomic_set(&lora_joined_flag, 0);
```

Problems with the current streak heuristic:
- Only counts confirmed frames, which we rarely send.
- Easy to trip from a single noisy hour and then we tear down + rejoin (expensive, and we lose session counters).
- Doesn't credit positive events that happened in between.

Replace with: EWMA over the last ~N confirmed attempts (or a 16-bit ring buffer). Trip the "demote to STALE" threshold at e.g. 80% recent failure rate. **Demoting to STALE is not the same as clearing `lora_joined_flag`** — it just means "next housekeeping tick, probe with `LinkCheckReq(force=true)` before doing anything expensive."

## Keep `-ENOTCONN` as the only *immediate* downgrade

`-ENOTCONN` from the MAC is authoritative (the stack itself says no session). That path can stay binary. Everything else should funnel through the STALE state and require a confirming probe.

## Add a lazy staleness watchdog in housekeeping

You already have a periodic thread that runs at known offsets. Once per HK tick:

```c
if (lora_is_joined()
    && lora_link_seconds_since_lns_evidence() > LINK_STALE_HOURS * 3600) {
    lora_request_link_check(true);   // piggybacks on next HK uplink
}
```

If the LinkCheckAns lands, `lora_link_health` updates and you're done — no rejoin, no airtime cost beyond a few MAC bytes. If after a few HK cycles nothing comes back, *then* you escalate to clearing `lora_joined_flag` and rejoining.

This is exactly the "doesn't need to know all the time" model — you only ask when something else was already going to transmit, and you let the answer ride RX1/RX2 for free.

## Piggyback LinkCheckReq on every Nth heartbeat

Cheapest possible proof-of-life. Add a counter in the HK path: every Nth heartbeat (say every 6 hours or every 4th HB) set `force=true` so a `LinkCheckReq` is appended to the next MCPS frame. Zero additional packets, zero additional duty-cycle cost — the LNS' `LinkCheckAns` is the evidence the watchdog needs. Most weeks the staleness watchdog won't ever have to do anything because the regular HB piggybacks keep `last_evidence` fresh.

## Cache "LNS heard us" across reboot (optional but nice)

`join_state_store` only holds `has_joined_once`. Two extra fields make the install/UX flow much better:

- `last_evidence_epoch` (uint32) — last time we had LNS contact
- `last_demod_margin` (int8) + `last_nb_gateways` (uint8)

Then on every cold boot:
- Show "last seen LNS N hours ago, X gateways, margin Y dB" in the installer screen.
- Decide *more intelligently* whether to do the silent rejoin immediately, or wait — if last evidence is < 1 hour old you can probably just send 1 HB instead of full OTAA (the session keys in flash, if you persist them, are already valid).

This is also a much better answer to "what's currently going on?" than `has_joined_once=1` alone.

## What I'd actually remove or simplify

- **`consecutive_send_failures` global** — folded into `lora_link_health`'s EWMA.
- **`LORA_SEND_FAILURES_BEFORE_BACKOFF` immediate-clear path** — replaced by demote-to-STALE + watchdog escalation.
- **`LORA_POST_JOIN_MAC_PROBE_RETRIES`** — keep the *concept*, but make it the first sample fed into `lora_link_health` rather than a special boot path with its own retry knobs. One code path for "probe and learn," not two.
- **`dr_time_sync_retry_requested`** — the broader "have we ever heard from LNS this join?" signal subsumes it; you can drop the one-shot guard once `lora_link_health` tracks join-relative evidence.
- **The `lora_link_stats_have_sample()` API** — collapses into `lora_link_state() != UNKNOWN`.

## Suggested order of work

1. **Add `lora_link_health` module** (atomic timestamp + EWMA). Wire the hooks in `lora_app_dl_callback`, `lora_app_dr_changed`, `lora_link_stats` LinkCheckAns path, and the confirmed-uplink success path. No behavior change yet — just observability.
2. **Add `lora_link_state()` getter and log it** every HK tick. Run for a few days; verify HEALTHY rarely flips.
3. **Migrate `consecutive_send_failures` → EWMA** and change the trip point to "demote to STALE + force LinkCheckReq," not "clear joined."
4. **Add the HK staleness watchdog** with N-cycle escalation before tearing down session.
5. **Piggyback LinkCheckReq every Nth heartbeat** so the watchdog rarely has to act.
6. (Optional) **Persist `last_evidence_epoch` + last margin** in `join_state_store` for the installer UX.

The net result: instead of "we discover we lost the LNS the next time a confirmed frame happens to time out 5 times in a row," you have "we passively notice the LNS hasn't said anything in `LINK_STALE_HOURS`, ask cheaply, and only rejoin if it really is gone." It costs essentially zero airtime and removes two ad-hoc heuristics from the LoRa thread.

Want me to draft `lora_link_health.{h,c}` and the callback wiring as step 1?

