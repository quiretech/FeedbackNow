# FlexBox v1.2 — Build and production

## Layout

- **Headers:** `include/` (single place for `.h` and `sys_config.h`)
- **Sources:** `src/` (`.c` only)

## Build and flash

From repo root or `app/`:

```text
west build -b nrf52840dk/nrf52840
west flash --runner nrfjprog
```

Serial console: 115200 8N1.

## Production build (default)

- **Path:** Real buttons only. Input → SMF → app logic; no demo/pseudo simulation.
- **Logging:** Default log level is INFO. Mode transitions (e.g. Normal → Staff, Staff → Normal) and errors are at INFO; per-event and combo details are at DBG.
- **EEPROM probe:** At boot, one line "EEPROM probe OK" unless `SYS_CONFIG_EEPROM_PROBE_LOG=1` in `include/sys_config.h` (then hex dump is logged).

## Bring-up / debug

- **Verbose Input and SMF:** In `prj.conf` set `CONFIG_LOG_DEFAULT_LEVEL_DBG=4` (or use module-level override) to see all `[Input]` and `[SMF]` traces.
- **EEPROM hex at boot:** Set `SYS_CONFIG_EEPROM_PROBE_LOG 1` in `include/sys_config.h`.

## Compile-time flags (sys_config.h)

| Flag | Effect |
|------|--------|
| `EEPROM_COUNTERS_FACTORY_RESET_ON_BOOT` | 1 = wipe button counters on next boot, then reboot (one-shot). |
| `EEPROM_DEVNONCE_FACTORY_RESET_ON_BOOT` | 1 = reinit DevNonce store on next boot, then reboot (one-shot). |
| `EEPROM_JOIN_STATE_CLEAR_ON_BOOT` | 1 = behave as first boot (no auto-join; wait for Staff+0+1+2). Use for testing. |
| `SYS_CONFIG_EEPROM_PROBE_LOG` | 1 = log EEPROM probe hex dump at boot. |

All other tunables (timeouts, combo holds, LED timings, etc.) are in `include/sys_config.h` with section comments.
