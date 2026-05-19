#ifndef MAPEK_EXECUTE_H
#define MAPEK_EXECUTE_H

#include "mapek/mapek_knowledge.h"
#include "mapek/mapek_types.h"

void mapek_execute_run(const plan_out_t *plan, const analyze_out_t *ana,
                       mapek_knowledge_t *kb, uint32_t now);

#endif /* MAPEK_EXECUTE_H */
