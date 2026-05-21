#ifndef DISPLAY_STRINGS_H
#define DISPLAY_STRINGS_H

/*
 * EPD on-screen copy and install-screen labels (compile-time locale).
 * Included from sys_config.h when EPD_ENABLED=1.
 *
 * Full-screen bitmaps: EPD_LOCALE_FR_BITMAPS in sys_config.h (0=EN, 1=FR).
 */

#if EPD_LOCALE_FR_BITMAPS
#define EPD_TEXT_LAST_CLEANED_HEADLINE "DERNIER NETTOYAGE"
#else
#define EPD_TEXT_LAST_CLEANED_HEADLINE "LAST CLEANED"
#endif

#define EPD_TEXT_CONNECTING "Connecting..."
#define EPD_TEXT_BRAND_TITLE "flexbox"
#define EPD_TEXT_MANUFACTURER "quire.tech"
#define EPD_TEXT_FW_PREFIX "fw  "

/** Demod margin (dB) thresholds for 4-tier link label on device-info screen. */
#define EPD_LINK_MARGIN_EXCELLENT_DB 20
#define EPD_LINK_MARGIN_GOOD_DB 10
#define EPD_LINK_MARGIN_FAIR_DB 3

#define EPD_STATUS_LABEL_LINK "link:"
#define EPD_STATUS_LABEL_GATEWAYS "gateway(s):"
#define EPD_STATUS_LABEL_MARGIN "margin:"
#define EPD_STATUS_GATEWAYS_EMPTY "gateway(s): --"
#define EPD_STATUS_MARGIN_EMPTY "margin: --"
#define EPD_STATUS_LABEL_STATUS "status:"
#define EPD_STATUS_LINK_EXCELLENT "excellent"
#define EPD_STATUS_LINK_GOOD "good"
#define EPD_STATUS_LINK_FAIR "fair"
#define EPD_STATUS_LINK_WEAK "weak"
#define EPD_STATUS_LINK_NO_RESPONSE "no response"
#define EPD_STATUS_JOINED "joined"
#define EPD_STATUS_NOT_JOINED "not joined"
#define EPD_STATUS_VALUE_NONE "--"
#define EPD_STATUS_MARGIN_SUFFIX " dB"
#define EPD_STATUS_FW_PREFIX "fw: "

#endif /* DISPLAY_STRINGS_H */
