//
// Created by Arnau Sanz on 16/8/25.
//

#include "sparse_set.h"

//TODO: think of void*, maybe need macros, don't like having opaque type + requires calling the ss_get when iterating (less cache friendly I think)

SparseSet ss_create(const size_t component_size, const Entity initial_capacity) {
    SparseSet set = {0};
    set.component_size = component_size;
    set.capacity = initial_capacity;
    set.dense = SDL_malloc(component_size * initial_capacity);
    set.dense_entities = SDL_malloc(sizeof(Entity) * initial_capacity);
    set.size = 0;
    SDL_memset(set.sparse, ENTITY_MAX, sizeof(Entity) * ENTITY_MAX); // ENTITY_MAX means no component
    return set;
}

void ss_destroy(const SparseSet *const set) {
    SDL_free(set->dense);
    SDL_free(set->dense_entities);
}

void ss_add(SparseSet *const set, const Entity e, const void *const component) {
    SDL_assert(e < ENTITY_MAX && "Entity index out of bounds");
    if (set->sparse[e] == ENTITY_MAX) {
        if (set->size >= set->capacity) {
            set->capacity *= 2;
            set->dense = SDL_realloc(set->dense, set->component_size * set->capacity);
            set->dense_entities = SDL_realloc(set->dense_entities, sizeof(Entity) * set->capacity);
        }
        SDL_memcpy((char *)set->dense + set->size * set->component_size, component, set->component_size);
        set->dense_entities[set->size] = e;  //track which entity owns this slot
        set->sparse[e] = set->size;
        set->size++;
    }
}

void ss_remove(SparseSet *const set, const Entity e) {
    SDL_assert(e < ENTITY_MAX && "Entity index out of bounds");
    if (set->sparse[e] != ENTITY_MAX) {
        const Entity removed_index = set->sparse[e];
        const Entity last_index = set->size - 1;

        if (removed_index != last_index) {
            //swap last component into removed slot
            SDL_memcpy((char *)set->dense + removed_index * set->component_size,
                   (char *)set->dense + last_index * set->component_size,
                   set->component_size);

            const Entity moved_entity = set->dense_entities[last_index];
            set->sparse[moved_entity] = removed_index;
            set->dense_entities[removed_index] = moved_entity;
        }

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
    return set->dense_entities[index];  // O(1) lookup via parallel array
}
