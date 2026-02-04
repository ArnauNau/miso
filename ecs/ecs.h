//
// Created by Arnau Sanz on 16/8/25.
//

#ifndef MISO_ECS_H
#define MISO_ECS_H
#include "sparse_set.h"

typedef struct ECSWorld_ {
    SparseSet cameras;      // Camera2D_Component
    SparseSet smooth_zooms; // SmoothZoom_Component
    bool entities[ENTITY_MAX];
} ECSWorld;

ECSWorld ECS_create();
void ECS_destroy(const ECSWorld *world);
Entity ECS_create_entity(ECSWorld *world);

#endif //MISO_ECS_H
