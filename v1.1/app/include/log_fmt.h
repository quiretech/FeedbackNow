#ifndef LOG_FMT_H
#define LOG_FMT_H

/*
 * One-line milestones (avoid blank lines + multi-line banners; module tag
 * already identifies the subsystem in Zephyr's log prefix).
 */

#include <zephyr/logging/log.h>

#define LOG_SECTION_INF(title)  LOG_INF("== %s ==", title)
#define LOG_SECTION_WRN(title)    LOG_WRN("== %s ==", title)
#define LOG_SECTION_ERR(title) LOG_ERR("== %s ==", title)

#endif /* LOG_FMT_H */
