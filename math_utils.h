//
// Created by Arnau Sanz on 14/12/25.
//

#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <math.h>

// Linear interpolation between a and b by factor t (0..1)
static inline float lerpf(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

// Frame-rate independent exponential decay toward target.
// Decays ~63% of the remaining distance per (1/speed) seconds.
// Use speed ~5-15 for smooth camera movements.
static inline float exp_decayf(const float current, const float target, const float speed, const float dt) {
    return lerpf(current, target, 1.0f - expf(-speed * dt));
}

#endif //MATH_UTILS_H
