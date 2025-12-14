//
// Created by Arnau Sanz on 16/8/25.
//

#include "sparse_set.h"

//TODO: think of void*, maybe need macros, don't like having opaque type + requires calling the ss_get when iterating (less cache friendly I think)

SparseSet ss_create(const size_t component_size, const Entity initial_capacity) {
    SparseSet set = {nullptr, {0}, 0, 0, 0};
    set.component_size = component_size;
    set.capacity = initial_capacity;
    set.dense = SDL_malloc(component_size * initial_capacity);
    set.size = 0;
    SDL_memset(set.sparse, ENTITY_MAX, sizeof(Entity) * ENTITY_MAX); //value of ENTITY_MAX means no component for that entity
    return set;
}

void ss_destroy(const SparseSet *const set) {
    SDL_free(set->dense);
}

void ss_add(SparseSet *const set, const Entity e, const void *const component) {
    SDL_assert(e < ENTITY_MAX && "Entity index out of bounds");
    if (set->sparse[e] == ENTITY_MAX) {
        if (set->size >= set->capacity) {
            set->capacity *= 2;
            set->dense = SDL_realloc(set->dense, set->component_size * set->capacity);
        }
        SDL_memcpy((char *)set->dense + set->size * set->component_size, component, set->component_size);
        set->sparse[e] = set->size;
        set->size++;
    }
}

void ss_remove(SparseSet *const set, const Entity e) {
    SDL_assert(e < ENTITY_MAX && "Entity index out of bounds");
    if (set->sparse[e] != ENTITY_MAX) {
        const uint32_t index = set->sparse[e];
        SDL_memcpy((char *)set->dense + index * set->component_size,
               (char *)set->dense + (set->size - 1) * set->component_size,
               set->component_size);
        set->sparse[e] = ENTITY_MAX;
        set->size--;
    }
}

bool ss_has(const SparseSet *const set, const Entity e) {
    SDL_assert(e < ENTITY_MAX && "Entity index out of bounds");
    return set->sparse[e] != ENTITY_MAX;
}

void *ss_get(const SparseSet *const set, const Entity e) {
    return ss_has(set, e) ? (char *)set->dense + set->sparse[e] * set->component_size : nullptr;
}

Entity ss_get_entity(const SparseSet *const set, const Entity index) {
    SDL_assert(index < set->size && "Index out of bounds");
    for (Entity e = 0; e < ENTITY_MAX; e++) {
        if (set->sparse[e] == index) {
            return e;
        }
    }
    return ENTITY_MAX; // Not found
}
