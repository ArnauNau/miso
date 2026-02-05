#include "miso_buildings.h"

#include "internal/miso__world_internal.h"

#include <SDL3/SDL.h>

static bool miso__ensure_building_capacity(MisoWorld *world) {
    if (world->building_count < world->building_capacity) {
        return true;
    }

    const uint32_t new_capacity = world->building_capacity == 0 ? 64U : world->building_capacity * 2U;
    MisoBuildingRecord *new_records = SDL_realloc(world->buildings, sizeof(MisoBuildingRecord) * new_capacity);
    if (!new_records) {
        return false;
    }

    SDL_memset(new_records + world->building_capacity,
               0,
               sizeof(MisoBuildingRecord) * (new_capacity - world->building_capacity));
    world->buildings = new_records;
    world->building_capacity = new_capacity;
    return true;
}

MisoPlacementFail miso_building_can_place(const MisoWorld *world, const MisoPlacementQuery *query) {
    if (!world || !query || query->footprint_w <= 0 || query->footprint_h <= 0) {
        return MISO_PLACE_RULE_VIOLATION;
    }

    for (int y = 0; y < query->footprint_h; y++) {
        for (int x = 0; x < query->footprint_w; x++) {
            const int tx = query->tx + x;
            const int ty = query->ty + y;
            if (tx < 0 || ty < 0 || tx >= world->map.width_tiles || ty >= world->map.height_tiles) {
                return MISO_PLACE_OUT_OF_BOUNDS;
            }
            if (!miso_world_is_tile_free(world, tx, ty)) {
                return MISO_PLACE_BLOCKED;
            }
        }
    }

    return MISO_PLACE_OK;
}

MisoResult miso_building_place(MisoWorld *world,
                               MisoBuildingTypeId type_id,
                               int tx,
                               int ty,
                               int footprint_w,
                               int footprint_h,
                               MisoBuildingId *out_id) {
    if (!world || footprint_w <= 0 || footprint_h <= 0) {
        return MISO_ERR_INVALID_ARG;
    }

    const MisoPlacementQuery query = {
        .type_id = type_id,
        .tx = tx,
        .ty = ty,
        .footprint_w = footprint_w,
        .footprint_h = footprint_h,
    };

    if (miso_building_can_place(world, &query) != MISO_PLACE_OK) {
        return MISO_ERR_INVALID_ARG;
    }

    if (!miso__ensure_building_capacity(world)) {
        return MISO_ERR_OUT_OF_MEMORY;
    }

    MisoBuildingRecord *record = &world->buildings[world->building_count++];
    record->id = world->next_building_id++;
    record->type_id = type_id;
    record->tx = tx;
    record->ty = ty;
    record->footprint_w = footprint_w;
    record->footprint_h = footprint_h;
    record->active = true;

    for (int y = 0; y < footprint_h; y++) {
        for (int x = 0; x < footprint_w; x++) {
            miso_world_set_tile_occupied(world, tx + x, ty + y, true);
        }
    }

    if (out_id) {
        *out_id = record->id;
    }

    return MISO_OK;
}

MisoResult miso_building_remove(MisoWorld *world, MisoBuildingId building_id) {
    if (!world || building_id == 0) {
        return MISO_ERR_INVALID_ARG;
    }

    for (uint32_t i = 0; i < world->building_count; i++) {
        MisoBuildingRecord *record = &world->buildings[i];
        if (!record->active || record->id != building_id) {
            continue;
        }

        for (int y = 0; y < record->footprint_h; y++) {
            for (int x = 0; x < record->footprint_w; x++) {
                miso_world_set_tile_occupied(world, record->tx + x, record->ty + y, false);
            }
        }

        record->active = false;
        return MISO_OK;
    }

    return MISO_ERR_NOT_FOUND;
}

bool miso_building_pick_at_screen(
    const MisoWorld *world, const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy, MisoBuildingId *out_id) {
    if (!world || !engine || !out_id) {
        return false;
    }

    int tx = 0;
    int ty = 0;
    if (!miso_world_screen_to_tile(world, engine, camera_id, sx, sy, &tx, &ty)) {
        return false;
    }

    for (uint32_t i = 0; i < world->building_count; i++) {
        const MisoBuildingRecord *record = &world->buildings[i];
        if (!record->active) {
            continue;
        }

        if (tx >= record->tx && ty >= record->ty && tx < record->tx + record->footprint_w &&
            ty < record->ty + record->footprint_h) {
            *out_id = record->id;
            return true;
        }
    }

    return false;
}

int miso_building_get_all(const MisoWorld *world, MisoBuildingInfo *out_items, int capacity) {
    if (!world || !out_items || capacity <= 0) {
        return 0;
    }

    int written = 0;
    for (uint32_t i = 0; i < world->building_count && written < capacity; i++) {
        const MisoBuildingRecord *record = &world->buildings[i];
        if (!record->active) {
            continue;
        }

        out_items[written++] = (MisoBuildingInfo){
            .id = record->id,
            .type_id = record->type_id,
            .tx = record->tx,
            .ty = record->ty,
            .footprint_w = record->footprint_w,
            .footprint_h = record->footprint_h,
        };
    }

    return written;
}
