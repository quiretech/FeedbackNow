#ifndef DISPLAY_STRINGS_H
#define DISPLAY_STRINGS_H

/*
 * EPD on-screen copy and install-screen labels (compile-time locale).
 * Included from sys_config.h when EPD_ENABLED=1.
 *
 * Full-screen bitmaps: EPD_LOCALE in sys_config.h (EN / FR / DE / ES).
 */

#if EPD_LOCALE == EPD_LOCALE_FR
#define EPD_TEXT_LAST_CLEANED_HEADLINE "DERNIER NETTOYAGE"
#elif EPD_LOCALE == EPD_LOCALE_DE
#define EPD_TEXT_LAST_CLEANED_HEADLINE "LETZTE REINIGUNG"
#elif EPD_LOCALE == EPD_LOCALE_ES
#define EPD_TEXT_LAST_CLEANED_HEADLINE "ÚLTIMA LIMPIEZA"
#else
#define EPD_TEXT_LAST_CLEANED_HEADLINE "LAST CLEANED"
#endif

// Room Turnaround text for the display screen
#define EPD_TEXT_NEXT_ACTION_HEADLINE    "NEXT: "
#define EPD_TEXT_PREPPING_PT_EXIT        "Prepping patient exit"
#define EPD_TEXT_2_CASE_CART_OUT         "2 Case cart out"
#define EPD_TEXT_CASE_CART_IS_OUT        "Case cart is out"
#define EPD_TEXT_3_EVS_IN                "3 EVS in"
#define EPD_TEXT_EVS_IS_IN               "EVS is in"
#define EPD_TEXT_4_BED_IS_BEING_WIPED    "4 Bed is being wiped"
#define EPD_TEXT_BED_HAS_BEEN_WIPED      "Bed has been wiped"
#define EPD_TEXT_5_EVS_OUT               "5 EVS out"
#define EPD_TEXT_EVS_IS_OUT              "EVS is out"
#define EPD_TEXT_6_PATIENT_IN            "6 Patient in"
#define EPD_TEXT_PATIENT_HAS_EXITED      "Patient has exited"
#define EPD_TEXT_PATIENT_IS_IN           "Patient is in"
#define EPD_TEXT_CYCLE                   "CYCLE"
#define EPD_TEXT_COMPLETED               "COMPLETED"


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
