#ifndef LOG_FMT_H
#define LOG_FMT_H

/*
 * Small logging helpers to keep console output consistent and easy to scan.
 *
 * Usage:
 *   LOG_SECTION_INF("SOME EVENT");
 *   LOG_SECTION_WRN("WARNING EVENT");
 */

#include <zephyr/logging/log.h>

#define LOG_SECTION_INF(title)                                                  \
  do {                                                                          \
    LOG_INF("");                                                                \
    LOG_INF("=== %s ===", title);                                               \
  } while (0)

#define LOG_SECTION_WRN(title)                                                  \
  do {                                                                          \
    LOG_WRN("");                                                                \
    LOG_WRN("=== %s ===", title);                                               \
  } while (0)

#define LOG_SECTION_ERR(title)                                                  \
  do {                                                                          \
    LOG_ERR("");                                                                \
    LOG_ERR("=== %s ===", title);                                               \
  } while (0)

#endif /* LOG_FMT_H */


