#ifndef MAPEK_PLAN_H
#define MAPEK_PLAN_H

#include "mapek/mapek_knowledge.h"
#include "mapek/mapek_types.h"

void mapek_plan_run(const analyze_out_t *ana, const mapek_knowledge_t *kb,
                    uint32_t now, plan_out_t *out);

#endif /* MAPEK_PLAN_H */
