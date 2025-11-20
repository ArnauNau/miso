//
// Created by Arnau Sanz on 16/8/25.
//

#ifndef NAU_ENGINE_CAMERA_H
#define NAU_ENGINE_CAMERA_H

#include <SDL3/SDL.h>
#include "../entity.h"
#include "../ecs.h"



typedef struct Camera2D_ {
    SDL_FPoint  position;           //camera position in world coordinates (screen center) //TODO: change to PositionComponent or TransformComponent or something
    float       zoom;               //camera zoom level
    SDL_Rect    viewport;           //camera viewport in screen coordinates
    bool        pixel_snap;         //snap to integers to avoid blurry lines (subpixel rendering issues)
} Camera2D;

typedef struct Camera2D_Component_ {
    // Entity   entity;               //entity this camera is attached to
    Camera2D camera;                //camera data
} Camera2D_Component;

typedef struct SmoothZoom_Component_ {
    float target_zoom;         //target zoom level
    float speed;          //speed at which the camera zooms in/out
} SmoothZoom_Component;


/* COMPONENTS */
void CAMERA_add(ECSWorld *world, Entity entity, Camera2D camera);
void CAMERA_zoom_apply(ECSWorld *world, Entity entity, float zoom_direction, float speed);
void CAMERA_zoom_set(Camera2D_Component *camera, float zoom, SDL_FPoint mouse_position);

/* SYSTEMS */
void CAMERA_smooth_zoom_system(ECSWorld *world, float dt, SDL_FPoint mouse_position);




/*      UTILITIES       */
//NOTE: ISOMETRIC WORLD!!! North orientation is up, East is right, South is down, West is left.
inline SDL_FPoint cam_world_to_screen(const Camera2D *c, float wx, float wy);

inline SDL_FPoint cam_screen_to_world(const Camera2D *c, float sx, float sy);

inline void cam_get_render_params(const Camera2D *c, float *scale, float *offx, float *offy);

#endif //NAU_ENGINE_CAMERA_H
