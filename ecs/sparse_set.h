//
// Created by Arnau Sanz on 16/8/25.
//

#ifndef NAU_ENGINE_SPARSE_SET_H
#define NAU_ENGINE_SPARSE_SET_H

#include <stdint.h>
#include <SDL3/SDL.h>
#include "entity.h"

typedef struct SparseSet_ {
    void *dense;                // packed array of components
    Entity *dense_entities;     // dense_entities[index] = entity that owns this slot (parallel to dense)
    Entity sparse[ENTITY_MAX];  // sparse[entity] = index in dense, or ENTITY_MAX
    Entity size;                // number of active components
    Entity capacity;            // allocated size
    size_t component_size;      // size of each component
} SparseSet;

SparseSet ss_create(size_t component_size, Entity initial_capacity);
void ss_destroy(const SparseSet *set);
void ss_add(SparseSet *set, Entity e, const void *component);
void ss_remove(SparseSet *set, Entity e);
bool ss_has(const SparseSet *set, Entity e);
void *ss_get(const SparseSet *set, Entity e);
Entity ss_get_entity(const SparseSet *set, uint16_t index);



#endif //NAU_ENGINE_SPARSE_SET_H
