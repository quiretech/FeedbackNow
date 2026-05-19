#ifndef MAPEK_ANALYZE_H
#define MAPEK_ANALYZE_H

#include "mapek/mapek_knowledge.h"
#include "mapek/mapek_types.h"

void mapek_analyze_run(const mon_snap_t *mon, const mapek_knowledge_t *kb,
                       analyze_out_t *out);

#endif /* MAPEK_ANALYZE_H */
