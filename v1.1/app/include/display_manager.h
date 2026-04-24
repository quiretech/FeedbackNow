/**
 * Display manager: EPD screen state and work queue.
 * When EPD_ENABLED=0, all APIs are no-ops.
 */
#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "sys_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum display_screen_id {
  DISPLAY_SCREEN_LOGO,
  DISPLAY_SCREEN_LAST_CLEANED,
  DISPLAY_SCREEN_THANKS,
  DISPLAY_SCREEN_CLEANING,
  DISPLAY_SCREEN_CONNECTING,
  DISPLAY_SCREEN_DEVICE_INFO,
#if EPD_INSTALL_INFO_SCREEN
  DISPLAY_SCREEN_INSTALL_INFO,
#endif
  DISPLAY_SCREEN_COUNT
};

/** Call once after last_cleaned_store and (if EPD) display driver are ready. */
int display_manager_init(void);

/** Show logo ("FeedBackNow FlexBox"). */
void display_show_logo(void);

/**
 * Show logo and block until EPD SPI is done. Use before starting LoRa join when
 * EPD and radio share SPI so JoinAccept RX is not starved by a long EPD flush.
 */
void display_show_logo_sync(void);

/**
 * Block until no EPD job is running and the display queue is idle (shared SPI
 * free for LoRa). timeout_ms: max wait; use UINT32_MAX to wait indefinitely.
 * No-op when EPD is disabled.
 */
void display_wait_until_spi_idle(uint32_t timeout_ms);

/** Show last cleaned: read from store (or use pending from downlink 0x01), then render. */
void display_show_last_cleaned(void);

/**
 * Show last cleaned and block until EPD SPI flush completes (0 ms queue delay).
 * Use after NFC check-out (and similar) so the panel updates immediately before
 * LoRa uplink on the shared SPI bus.
 */
void display_show_last_cleaned_sync(void);

/** Show thanks; starts timer then transitions to last cleaned. */
void display_show_thanks(void);

/** Show thanks and block until EPD render completes. Use for button: EPD first,
 * then LoRa/EEPROM (clear SPI for downlinks). */
void display_show_thanks_sync(void);

/** Show cleaning; starts 45min auto-revert timer. */
void display_show_cleaning(void);

/**
 * Show cleaning and block until EPD SPI flush completes (0 ms queue delay).
 * Use after NFC check-in so the panel updates right after the confirm blink.
 */
void display_show_cleaning_sync(void);

/** Show connecting. */
void display_show_connecting(void);

/** Show device info (async; uses DISPLAY_WORK_DELAY_MS after uplink-prone paths). */
void display_show_device_info(void);

/**
 * Show device info with 0 ms queue delay and block until EPD SPI flush completes.
 * Use from SMF after LED/rail changes so staff combo → LED → panel stay ordered.
 */
void display_show_device_info_sync(void);

#if EPD_INSTALL_INFO_SCREEN
/**
 * Show install / commissioning info (link quality, margin, gateways, unit id,
 * DevEUI, scannable QR code). Renders from current lora_link_stats snapshot;
 * if no LinkCheckAns has arrived, link quality falls back to WEAK and margin
 * is shown as "--". Async variant enqueues with 0 ms delay.
 */
void display_show_install_info(void);

/**
 * Same as display_show_install_info() but blocks until EPD flush completes.
 * Use from SMF after first-boot JOINED (or JOIN_CYCLE_FAILED) so LoRa/EEPROM
 * follow-ups run with clear SPI.
 */
void display_show_install_info_sync(void);
#endif

/**
 * Set pending last cleaned epoch (from downlink 0x01). Applied when we next show
 * last cleaned (after Thanks+5s or immediately).
 */
void display_set_pending_last_cleaned(uint32_t epoch);

/**
 * Set pending and apply now if not currently showing Thanks (else apply after Thanks+5s).
 */
void display_set_pending_last_cleaned_and_apply(uint32_t epoch);

/** Enqueue full refresh (clear ghosting). */
void display_request_full_refresh(void);

/**
 * True while a public-vote ack is in progress: THANKS has started (sync path)
 * until the post-thanks screen has finished rendering — LAST_CLEANED, or
 * CLEANING again when a cleaning session is active. When true, ignore further
 * public votes. EPD builds only; always false when EPD is disabled.
 */
bool display_is_public_vote_ui_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MANAGER_H */
