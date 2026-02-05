#ifndef MISO__WORLD_INTERNAL_H
#define MISO__WORLD_INTERNAL_H

#include "miso_buildings.h"

typedef struct MisoBuildingRecord {
    MisoBuildingId id;
    MisoBuildingTypeId type_id;
    int tx;
    int ty;
    int footprint_w;
    int footprint_h;
    bool active;
} MisoBuildingRecord;

struct MisoWorld {
    MisoEngine *engine;
    MisoIsoMapDesc map;
    bool *occupied;

    MisoBuildingRecord *buildings;
    uint32_t building_count;
    uint32_t building_capacity;
    uint32_t next_building_id;
};

#endif
