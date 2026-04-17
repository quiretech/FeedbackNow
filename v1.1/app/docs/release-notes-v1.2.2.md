# FlexBox Firmware v1.2.2

## Stability Remediation

- Added deterministic boot handshakes so worker threads acknowledge readiness before final system-go transitions.
- Removed LoRa thread blocking on time sync completion; time sync completion is now posted asynchronously to SMF.
- Improved post-join ADR warmup flow so time sync is requested after DR stabilization or warmup timeout.
- Hardened RTC access around 3V3A power sequencing with settle-time guarding and init retries.
- Removed shared SMF payload buffers for downlink/NFC events; payload data now travels with each queued SMF message.
- Enabled partial EPD refresh mode in SSD1683 display write path and reduced startup delay for logo/connecting jobs.
- Added queue-overflow diagnostics for button and SMF message paths.
- Tuned stack/queue defaults and enabled stack instrumentation (`CONFIG_THREAD_STACK_INFO`, `CONFIG_INIT_STACKS`).

## Version

- Firmware version string updated from `1.2.1` to `1.2.2`.
