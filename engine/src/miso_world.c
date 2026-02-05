#include "miso_world.h"

#include "internal/miso__world_internal.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

static bool miso__in_bounds(const MisoWorld *world, int tx, int ty) {
    return world && tx >= 0 && ty >= 0 && tx < world->map.width_tiles && ty < world->map.height_tiles;
}

static int miso__tile_index(const MisoWorld *world, int tx, int ty) {
    return ty * world->map.width_tiles + tx;
}

MisoWorld *miso_world_create(MisoEngine *engine, const MisoIsoMapDesc *desc) {
    if (!engine || !desc || desc->width_tiles <= 0 || desc->height_tiles <= 0 || desc->tile_w_px <= 0 ||
        desc->tile_h_px <= 0) {
        return NULL;
    }

    MisoWorld *world = SDL_calloc(1, sizeof(MisoWorld));
    if (!world) {
        return NULL;
    }

    world->engine = engine;
    world->map = *desc;

    const size_t tile_count = (size_t)desc->width_tiles * (size_t)desc->height_tiles;
    world->occupied = SDL_calloc(tile_count, sizeof(bool));
    if (!world->occupied) {
        SDL_free(world);
        return NULL;
    }

    world->next_building_id = 1;
    return world;
}

void miso_world_destroy(MisoWorld *world) {
    if (!world) {
        return;
    }

    SDL_free(world->occupied);
    SDL_free(world->buildings);
    SDL_free(world);
}

bool miso_world_is_tile_free(const MisoWorld *world, int tx, int ty) {
    if (!miso__in_bounds(world, tx, ty)) {
        return false;
    }

    return !world->occupied[miso__tile_index(world, tx, ty)];
}

bool miso_world_set_tile_occupied(MisoWorld *world, int tx, int ty, bool occupied) {
    if (!miso__in_bounds(world, tx, ty)) {
        return false;
    }

    world->occupied[miso__tile_index(world, tx, ty)] = occupied;
    return true;
}

bool miso_world_screen_to_tile(const MisoWorld *world,
                               const MisoEngine *engine,
                               MisoCameraId camera_id,
                               int sx,
                               int sy,
                               int *out_tx,
                               int *out_ty) {
    if (!world || !engine || !out_tx || !out_ty) {
        return false;
    }

    const MisoVec2 world_pos = miso_camera_screen_to_world(engine, camera_id, sx, sy);

    const float iso_w = (float)world->map.tile_w_px;
    const float iso_h = (float)world->map.tile_h_px * 0.5f;
    const float start_x = ((float)(world->map.height_tiles - 1) * iso_w) * 0.5f;

    const float a = (world_pos.x - start_x) / (iso_w * 0.5f);
    const float b = world_pos.y / (iso_h * 0.5f);

    const int tx = (int)floorf((a + b) * 0.5f);
    const int ty = (int)floorf((b - a) * 0.5f);

    *out_tx = tx;
    *out_ty = ty;

    return miso__in_bounds(world, tx, ty);
}

const MisoIsoMapDesc *miso_world_get_desc(const MisoWorld *world) {
    if (!world) {
        return NULL;
    }
    return &world->map;
}
