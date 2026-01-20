#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

// Nuklear (declarations only, implementation in debug_ui.c)
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
#include "vendored/nuklear/nuklear.h"

#include "camera/camera.h"
#include "debug_ui.h"
#include "game_clock.h"
#include "profiler.h"
#include "renderer/renderer.h"
#include "renderer/ui.h"
#include "tilemap/tilemap.h"

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
static int screen_width = WINDOW_WIDTH;
static int screen_height = WINDOW_HEIGHT;
static float pixel_ratio = 0.0f;


int is_point_in_rect(const float x, const float y,
                     const SDL_FRect *const SDL_RESTRICT rect) {
  return (x >= rect->x && x <= rect->x + rect->w && y >= rect->y &&
          y <= rect->y + rect->h);
}

#define TILE_SIZE 32

typedef enum TileType_ {
    TILE_PLACEHOLDER_BUILDING = 48,
    TILE_PLACEHOLDER_TERRAIN = 18,
    TILE_PLACEHOLDER_SEA = 13,
    TILE_PLACEHOLDER_BOAT = 54
} TileType;

// typedef enum {
//     TYPE_BUILDING_TILE,
//     TYPE_TERRAIN_TILE,
//     TYPE_SEA_TILE,
//     TYPE_BOAT
// } EntityType;
//
// typedef struct SpriteDefinition_ {
//     int tile_index;
//     int width, height;
// } SpriteDefinition;
//
// static const SpriteDefinition sprite_definitions[] = {
//     [TYPE_BUILDING_TILE] = { TILE_PLACEHOLDER_BUILDING, 1, 1},
//     [TYPE_TERRAIN_TILE] = { TILE_PLACEHOLDER_TERRAIN, 1, 1},
//     [TYPE_SEA_TILE] = { TILE_PLACEHOLDER_SEA, 1, 1},
//     [TYPE_BOAT] = { TILE_PLACEHOLDER_BOAT, 2, 2},
// };

#define MAX_BUILDINGS 512

// NOTE: ECS stuff:
//  Track if an entity is in use. (p.e:
//       typedef uint32_t Entity;
//       #define MAX_ENTITIES 1024
//       bool entity_alive[MAX_ENTITIES])
//  Create/destroy with a pool allocator or free list.

typedef struct TransformComponent_ {
    int x, y;
} TransformComponent;

typedef struct BuildingComponent_ {
    // EntityType type;
    int width, length;
} BuildingComponent;

typedef struct RenderableComponent_ {
    int tile_index;
    int sprite_w, sprite_h;
} RenderableComponent;

static RenderableComponent renderables[MAX_BUILDINGS]; //TODO: think about allocations etc...
static TransformComponent transforms[MAX_BUILDINGS];
static BuildingComponent buildings[MAX_BUILDINGS];
static int building_count;

static bool wireframe_mode = false;
static bool debug_mode = false;
static bool vsync = true;

SDL_Point hover_tile = {-1, -1};


//TODO: idea, weather conditions like in P3, can affect this! also refine wave math formula so it's more like a real sea wave
static GameClock game_clock;
static float wave_speed = 0.2f;     // how fast does the wave cycle go
static float wave_amplitude = 0.5f; // how high are the waves (amplitude) in terms of tile height
static float wave_phase = 0.1f;     // how 'wide' are the waves (period)

// Draw debug highlight on hovered tile (perimeter + beacon)
void render_tile_highlight(const Tilemap *tilemap, int tile_x, int tile_y, SDL_FColor color) {
    if (tile_x < 0 || tile_y < 0 || tile_x >= tilemap->width || tile_y >= tilemap->height)
        return;

    float world_x, world_y;
    Tilemap_TileToWorld(tilemap, tile_x, tile_y, &world_x, &world_y);

    float iso_w, iso_h;
    Tileset_GetIsoDimensions(tilemap->tileset, &iso_w, &iso_h);

    // Diamond vertices for isometric floor (2:1 aspect ratio: iso_w x iso_h)
    const float top_x = world_x + iso_w / 2.0f;
    const float top_y = world_y;
    const float right_x = world_x + iso_w;
    const float right_y = world_y + iso_h / 2.0f;
    const float bottom_x = world_x + iso_w / 2.0f;
    const float bottom_y = world_y + iso_h;
    const float left_x = world_x;
    const float left_y = world_y + iso_h / 2.0f;

    // Depth for the tile
    const float depth = Tilemap_GetTileDepth(tilemap, tile_x, tile_y) - 0.002f;

    // Draw perimeter (4 lines forming diamond)
    Renderer_DrawLine(top_x, top_y, depth, right_x, right_y, depth, color);
    Renderer_DrawLine(right_x, right_y, depth, bottom_x, bottom_y, depth, color);
    Renderer_DrawLine(bottom_x, bottom_y, depth, left_x, left_y, depth, color);
    Renderer_DrawLine(left_x, left_y, depth, top_x, top_y, depth, color);

    // Draw beacon (vertical ray going up from top of tile)
    constexpr float beacon_height = 200.0f;
    Renderer_DrawLine(top_x, top_y, depth, top_x, top_y - beacon_height, depth, color);
}


typedef struct WireframeMesh_ {
    SDL_Vertex *verts;
    int vert_count;
} WireframeMesh;

static WireframeMesh wireframe_meshes[MAX_BUILDINGS];

// --- helper to build and cache a mesh ---
static WireframeMesh build_wireframe_mesh(
    const float iso_x, const float iso_y,
    const float iso_w, const float iso_h,
    const int bw, const int bl,
    const int sw, const int sh)
{
    const float half_w = iso_w*0.5f;
    const float half_h = iso_h*0.5f;
    const float tile_h = iso_h*2.0f;
    const float base_y = iso_y + tile_h*(float)sh;
    const SDL_FColor col = {0,255,255,255};
    WireframeMesh thin_mesh;

    {
        // estimate max lines and allocate twice as many vertices, aligned to a multiple of 3, for rendering constraints
        const int max_lines = (bw + bl) + bw*(sh+1) + bl*(sh+1) + (bw+1) * (bl+1);
        const int num_vertices = max_lines * 2 + (3 - ((max_lines * 2) % 3));
        SDL_Vertex *v = SDL_malloc(sizeof(SDL_Vertex) * (unsigned int)num_vertices);
        int idx = 0;

        // width-wise lines
        for (int x = 0; x < bw; x++) {
            const float vx = iso_x + half_w * (float)x;
            const float vb = base_y - half_h * (float)(bw-x);
            const float vt = vb - iso_h*(float)sh;
            // vertical
            v[idx++] = (SDL_Vertex){{vx,vb}, col,{0,0}};
            v[idx++] = (SDL_Vertex){{vx,vt}, col,{0,0}};
            // top faces
            for (int y = 0; y <= sh; y++) {
                const float dy = vb - iso_h*(float)y;
                v[idx++] = (SDL_Vertex){{vx,         dy},      col,{0,0}};
                v[idx++] = (SDL_Vertex){{vx+half_w,  dy+half_h},col,{0,0}};
            }
        }
        // length-wise lines
        for (int x = 0; x <= bl; x++) {
            const float vx = iso_x + iso_w * (float)(bw+x) * 0.5f;
            const float vb = base_y - iso_h * 0.5f * (float)x;
            const float vt = vb - iso_h * (float)sh;
            v[idx++] = (SDL_Vertex){{vx,vb}, col,{0,0}};
            v[idx++] = (SDL_Vertex){{vx,vt}, col,{0,0}};
            if (x < bl) {
                for (int y = 0; y <= sh; y++) {
                    const float dy = vb - iso_h*(float)y;
                    v[idx++] = (SDL_Vertex){{vx,           dy},         col,{0,0}};
                    v[idx++] = (SDL_Vertex){{vx+half_w,    dy-half_h},   col,{0,0}};
                }
            }
        }

        //roof side
        const SDL_FPoint roof_fl = {
            iso_x,
            base_y - half_h*(float)bw - iso_h*(float)sh
        }; // front-left
        const SDL_FPoint roof_fr = {
            iso_x + half_w*(float)bw,
            base_y - half_h*(float)bw - iso_h*(float)sh + half_h*(float)bw
        }; // front-right
        const SDL_FPoint roof_bl = {
            iso_x + half_w*(float)bl,
            roof_fl.y - half_h*(float)bl
        }; // back-left
        const SDL_FPoint roof_br = {
            iso_x + half_w*(bw+bl),
            roof_fr.y - half_h*bl
        }; // back-right

        const SDL_FPoint dv_len   = { half_w, -half_h };  // length direction
        const SDL_FPoint dv_width = { half_w,  half_h };  // width direction

        // lines parallel to length (splitting width units on the roof)
        for (int i = 1; i <= bl; i++) {
            SDL_FPoint p0 = {
                roof_fl.x + dv_len.x * (float) i,
                roof_fl.y + dv_len.y * (float) i
            };
            SDL_FPoint p1 = {
                roof_fr.x + dv_len.x * (float) i,
                roof_fr.y + dv_len.y * (float) i
            };
            v[idx++] = (SDL_Vertex){p0, col, {0, 0}};
            v[idx++] = (SDL_Vertex){p1, col, {0, 0}};
        }

        // lines parallel to width (splitting length units on the roof)
        for (int j = 0; j < bw; j++) {
            SDL_FPoint p0 = {
                roof_fl.x + dv_width.x * (float) j,
                roof_fl.y + dv_width.y * (float) j
            };
            SDL_FPoint p1 = {
                roof_bl.x + dv_width.x * (float) j,
                roof_bl.y + dv_width.y * (float) j
            };
            v[idx++] = (SDL_Vertex){p0, col, {0, 0}};
            v[idx++] = (SDL_Vertex){p1, col, {0, 0}};
        }

        const int remaining = num_vertices - idx;
        for (int i = 0; i < remaining; i++) {
            const SDL_Vertex last = v[idx - 1];
            v[idx++] = last;
        }

        thin_mesh = (WireframeMesh){.verts = v, .vert_count = idx};
    }

    // Each segment is two consecutive vertices in thin.verts
    const int segments = thin_mesh.vert_count / 2;
    const int max_verts = segments * 6;
    SDL_Vertex *v = SDL_malloc(sizeof(SDL_Vertex) * (unsigned int) max_verts);
    int idx = 0;

    for (int i = 0; i < segments; ++i) {
        constexpr float thickness = 1.0f;
        // endpoints
        SDL_FPoint p1 = thin_mesh.verts[2 * i].position;
        SDL_FPoint p2 = thin_mesh.verts[2 * i + 1].position;
        // direction & perpendicular
        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        const float len = SDL_sqrtf(dx * dx + dy * dy);
        if (len < 1e-6f)
            continue;
        dx /= len;
        dy /= len;
        const float nx = -dy * (thickness * 0.5f);
        const float ny = dx * (thickness * 0.5f);
        // quad corners
        const SDL_FPoint q1 = {p1.x + nx, p1.y + ny};
        const SDL_FPoint q2 = {p2.x + nx, p2.y + ny};
        const SDL_FPoint q3 = {p2.x - nx, p2.y - ny};
        const SDL_FPoint q4 = {p1.x - nx, p1.y - ny};
        // two triangles
        v[idx++] = (SDL_Vertex){q1, col, {0, 0}};
        v[idx++] = (SDL_Vertex){q2, col, {0, 0}};
        v[idx++] = (SDL_Vertex){q3, col, {0, 0}};
        v[idx++] = (SDL_Vertex){q3, col, {0, 0}};
        v[idx++] = (SDL_Vertex){q4, col, {0, 0}};
        v[idx++] = (SDL_Vertex){q1, col, {0, 0}};
    }

    // free the thin mesh
    SDL_free(thin_mesh.verts);
    return (WireframeMesh){.verts = v, .vert_count = idx};
}

void render_buildings(const Tilemap *const map, const TransformComponent *const ts,
                      const RenderableComponent *const rs, const int b_count,
                      const float offset_x, const float offset_y) {
    PROF_start(PROFILER_RENDER_BUILDINGS);

    const int tile_w = (int) map->tileset->tile_width;
    const int tile_h = (int) map->tileset->tile_height;
    const float iso_w = (float) tile_w;
    const float iso_h = (float) tile_h / 2.0f;
    const float start_x = offset_x + (float) (map->height - 1) * iso_w / 2.0f;
    const float start_y = offset_y;

    SpriteInstance *instances =
            SDL_malloc(sizeof(SpriteInstance) * (size_t)(b_count + 1));
    int instance_count = 0;

    const float tex_w = (float) (map->tileset->columns * map->tileset->tile_width);
    const float tex_h = (float) (map->tileset->rows * map->tileset->tile_height);

    for (int entity = 0; entity < b_count; entity++) {
        const int bw = rs[entity].sprite_w;
        const int bh = rs[entity].sprite_h;
        const int mx = ts[entity].x;
        const int my = ts[entity].y;

        float iso_x = start_x + (float) (mx - my) * (iso_w / 2.0f);
        float iso_y = start_y + (float) (mx + my) * (iso_h / 2.0f);

        iso_y -= (float) tile_h;
        iso_y -= (float) bh * iso_h;
        iso_x -= (float) (buildings[entity].width - 1) * 0.5f * iso_w;

        // UVs
        const int col = rs[entity].tile_index % (int)map->tileset->columns;
        const int row = rs[entity].tile_index / (int)map->tileset->columns;
        const float u = (float) (col * tile_w) / tex_w;
        const float v = (float) (row * tile_h) / tex_h;
        const float uw = (float) (bw * tile_w) / tex_w;
        const float vh = (float) (bh * tile_h) / tex_h;

        // Depth: same logic as tiles but with small bias so buildings appear in front;
        // invert so higher (mx+my) = lower depth = closer to camera
        const float depth =
                1.0f - (float) (mx + my) / (float) (map->width + map->height) - 0.001f;

        instances[instance_count++] = (SpriteInstance){
            .x = iso_x,
            .y = iso_y,
            .z = depth,
            .flags = 0.0f,       // Not water
            .w = (float) (bw * tile_w),
            .h = (float) (bh * tile_h),
            .tile_x = 0.0f,
            .tile_y = 0.0f,
            .u = u,
            .v = v,
            .uw = uw,
            .vh = vh
        };
    }

    Renderer_DrawSprites(map->tileset->texture, instances, instance_count);
    SDL_free(instances);

    PROF_stop(PROFILER_RENDER_BUILDINGS);
}

//TODO:
// - buildings (in general, trees also count, things that fill the map)
// TODO:
// - bounds checking
// - clickable entities (buildings, boats etc... even people????)
// - use ECS to animate tiles (adding transform components or enabling them whatever), first example would be animating the sea!
// - make some kind of 'fixedUpdate' to run logic (transforms, economy, etc), 'ticks'.

SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;

TTF_Font *font = nullptr;
TTF_Text *fps_text = nullptr;

#define MAP_SIZE_X 70
#define MAP_SIZE_Y 40
static Tileset *tileset = nullptr;
static Tilemap *tilemap = nullptr;
Entity main_camera;
ECSWorld ecs;

int initialize(void) {
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "1");

    if (SDL_Init(SDL_INIT_VIDEO) == false) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    if (TTF_Init() == false) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed: %s\n", SDL_GetError());
    }

    /* On Apple's macOS, **must** set the NSHighResolutionCapable Info. plist
     * property to YES, otherwise you will not receive a High-DPI OpenGL canvas.*/
    window = SDL_CreateWindow("NAU Engine", WINDOW_WIDTH, WINDOW_HEIGHT,
                             SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    if (!Renderer_Init(window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer_Init failed");
        return false;
    }

    SDL_GetWindowSizeInPixels(window, &screen_width, &screen_height);
    SDL_Log("Window real size: %d x %d", screen_width, screen_height);
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode *const display_mode = SDL_GetCurrentDisplayMode(display);
    SDL_Log("Display resolution: %d x %d", display_mode->w, display_mode->h);
    SDL_Log("Display DPI: %.2f", display_mode->pixel_density);
    pixel_ratio = display_mode->pixel_density;


    SDL_Surface *icon_surface = IMG_Load("icon.png");
    if (icon_surface) {
        SDL_SetWindowIcon(window, icon_surface);
        SDL_DestroySurface(icon_surface);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load icon: %s\n", SDL_GetError());
    }

    font =
            TTF_OpenFont("/Users/arnau/Library/Fonts/JetBrainsMono-Regular.ttf", 24);
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_OpenFont failed: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TTF_OpenFont failed", SDL_GetError(), window);
        SDL_DestroyWindow(window);
        return false;
    }

    TTF_TextEngine *engine = Renderer_GetTextEngine();
    if (engine) {
        fps_text = TTF_CreateText(engine, font, "FPS: 0", 0);
        PROF_initUI(engine, font);
    }

    UI_Init();

    // Initialize Nuklear debug UI with same font
    if (!DebugUI_Init("/Users/arnau/Library/Fonts/JetBrainsMono-Regular.ttf", 14.0f)) {
        SDL_Log("Warning: Failed to initialize debug UI");
    }

    tileset = Tileset_Load("isometric-sheet.png", TILE_SIZE, TILE_SIZE);
    if (tileset == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load tileset");
        return false;
    }

    tilemap = Tilemap_Create(MAP_SIZE_X, MAP_SIZE_Y, tileset);
    if (tilemap == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tilemap");
        return false;
    }

    // Initialize demo map: checkerboard pattern with sea in corner
    for (int y = 0; y < tilemap->height; y++) {
        for (int x = 0; x < tilemap->width; x++) {
            const int i = y * tilemap->width + x;
            int tile_index = (i % 2) ? 0 : 36;  // Checkerboard

            // Sea tiles in the far corner
            if (i > (tilemap->width * tilemap->height) - (tilemap->width / 2) * (tilemap->height / 2)) {
                tile_index = TILE_PLACEHOLDER_SEA;
                Tilemap_SetFlags(tilemap, x, y, TILE_FLAG_WATER);
            }

            Tilemap_SetTile(tilemap, x, y, tile_index);
        }
    }

    ecs = ECS_create();

    int w_w, w_h;
    SDL_GetWindowSizeInPixels(window, &w_w, &w_h);
    const Camera2D camera2d = {
        .position = {.x = 10.0f, .y = 10.0f},
        .zoom = 2.0f,
        .viewport = {0, 0, w_w, w_h},
        .pixel_snap = true
    };

    main_camera = ECS_create_entity(&ecs);
    CAMERA_add(&ecs, main_camera, camera2d);

    return true;
}

void handle_events(void) {
}

void update(void) {
}

void clean(void) {
    ECS_destroy(&ecs);

    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Bye!", "see you!", window);
    SDL_Log("Application quit.\n");

    for (int i = 0; i < building_count; i++) {
        SDL_free(wireframe_meshes[i].verts);
    }

    // Clean up tilemap (before renderer shutdown)
    Tilemap_Destroy(tilemap);
    Tileset_Destroy(tileset);

    if (fps_text)
        TTF_DestroyText(fps_text);

    DebugUI_Shutdown();
    PROF_deinitUI();
    UI_Shutdown();
    Renderer_Shutdown();
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}

// TODO: make sure not to spawn boats or buildings out of bounds (even partially)
void spawn_boat(const int x, const int y) {
    constexpr int b_w = 1;
    constexpr int b_l = 3; //NOTE: to test building width and length

    SDL_Log("tile index at bpl: %d", Tilemap_GetTile(tilemap, hover_tile.x, hover_tile.y));

    renderables[building_count].tile_index = TILE_PLACEHOLDER_BOAT;
    renderables[building_count].sprite_w = 2;
    renderables[building_count].sprite_h = 3;
    SDL_Log("Boat placed at: %d, %d", hover_tile.x, hover_tile.y);

    transforms[building_count].x = x;
    transforms[building_count].y = y;
    buildings[building_count].width = b_w;
    buildings[building_count].length = b_l;

    // Mark occupancy
    for (int dy = 0; dy > -b_l; dy--) {
        for (int dx = 0; dx > -b_w; dx--) {
            Tilemap_SetOccupied(tilemap, hover_tile.x + dx, hover_tile.y + dy, true);
            Tilemap_SetTile(tilemap, hover_tile.x + dx, hover_tile.y + dy, TILE_PLACEHOLDER_TERRAIN);
            // Remove water flag when placing building
            Tilemap_SetFlags(tilemap, hover_tile.x + dx, hover_tile.y + dy, TILE_FLAG_NONE);
        }
    }

    // Wireframe generation
    constexpr float b_h_ = 3.0f;
    constexpr float b_w_ = 1.0f;
    const float tile_w = (float) tilemap->tileset->tile_width;
    const float tile_h = (float) tilemap->tileset->tile_height;
    const float iso_w = tile_w;
    const float iso_h = tile_h * 0.5f;
    const float start_x = (float) (tilemap->height - 1) * iso_w * 0.5f;
    constexpr float start_y = 0.0f;
    const float iso_x = start_x + (float) (x - y) * iso_w * 0.5f - (b_w_ - 1) * iso_w * 0.5f;
    const float iso_y = start_y + (float) (x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
    wireframe_meshes[building_count] = build_wireframe_mesh(iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3);

    building_count++;
}

void spawn_boats(const int amount) {
    int count = 0;
    for (int y = tilemap->height - 1; y >= 2; y -= 3) {
        for (int x = 0; x < tilemap->width; x++) {
            if (unlikely(count >= amount || building_count >= MAX_BUILDINGS)) {
                return;
            }

            // Check if boat can fit (3 tiles vertically)
            if (Tilemap_IsTileFree(tilemap, x, y) &&
                Tilemap_IsTileFree(tilemap, x, y - 1) &&
                Tilemap_IsTileFree(tilemap, x, y - 2)) {

                // Place boat tiles and mark occupied
                for (int dy = 0; dy < 3; dy++) {
                    Tilemap_SetTile(tilemap, x, y - dy, TILE_PLACEHOLDER_TERRAIN);
                    Tilemap_SetOccupied(tilemap, x, y - dy, true);
                    Tilemap_SetFlags(tilemap, x, y - dy, TILE_FLAG_NONE);
                }

                // Renderables
                renderables[building_count].tile_index = TILE_PLACEHOLDER_BOAT;
                renderables[building_count].sprite_w = 2;
                renderables[building_count].sprite_h = 3;

                // Components
                transforms[building_count].x = x;
                transforms[building_count].y = y;
                buildings[building_count].width = 1;
                buildings[building_count].length = 3;

                // Wireframe
                constexpr float b_h_ = 3.0f;
                constexpr float b_w_ = 1.0f;
                const float tile_w = (float) tilemap->tileset->tile_width;
                const float tile_h = (float) tilemap->tileset->tile_height;
                const float iso_w = tile_w;
                const float iso_h = tile_h * 0.5f;
                const float start_x = (float) (tilemap->height - 1) * iso_w * 0.5f;
                constexpr float start_y = 0.0f;
                const float iso_x = start_x + (float) (x - y) * iso_w * 0.5f - (b_w_ - 1) * iso_w * 0.5f;
                const float iso_y = start_y + (float) (x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
                wireframe_meshes[building_count] = build_wireframe_mesh(iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3);

                building_count++;
                count++;
            }
        }
    }
}

int mainLoop(void) {
    int running = 1;
    float mouse_x = 0.0f, mouse_y = 0.0f;
    SDL_Point mouse_tile = {0, 0};

    game_clock = GameClock_create();

    while (running) {
        const float real_dt = PROF_getLastFrameTime() / 1000.0f;
        GameClock_update(&game_clock, real_dt);
        PROF_frameStart();

        // --- Event Handling ---
        PROF_start(PROFILER_EVENT_HANDLING);
        DebugUI_BeginInput();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Let debug UI handle event first
            PROF_stop(PROFILER_EVENT_HANDLING);
            PROF_start(PROFILER_NUKLEAR);
            if (DebugUI_HandleEvent(&event)) {
                PROF_stop(PROFILER_NUKLEAR);
                continue;
            }
            PROF_stop(PROFILER_NUKLEAR);
            PROF_start(PROFILER_EVENT_HANDLING);

            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = 0;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_ESCAPE:
                            running = 0;
                            break;
                        case SDLK_APOSTROPHE:
                            wireframe_mode = !wireframe_mode;
                            break;
                        case SDLK_P:
                            debug_mode = !debug_mode;
                            break;
                        case SDLK_PLUS:
                            spawn_boats(50);
                            break;
                        case SDLK_Z: {
                            Camera2D_Component *main_camera_component =
                                    ss_get(&ecs.cameras, main_camera);
                            main_camera_component->camera.position = (SDL_FPoint){0.0f, 0.0f};
                            main_camera_component->camera.zoom = 1.0f;
                            break;
                        }
                        case SDLK_W: {
                            Camera2D_Component *cam = ss_get(&ecs.cameras, main_camera);
                            CAMERA_pan(&cam->camera, 0, -1, real_dt);
                            break;
                        }
                        case SDLK_S: {
                            Camera2D_Component *cam = ss_get(&ecs.cameras, main_camera);
                            CAMERA_pan(&cam->camera, 0, 1, real_dt);
                            break;
                        }
                        case SDLK_A: {
                            Camera2D_Component *cam = ss_get(&ecs.cameras, main_camera);
                            CAMERA_pan(&cam->camera, -1, 0, real_dt);
                            break;
                        }
                        case SDLK_D: {
                            Camera2D_Component *cam = ss_get(&ecs.cameras, main_camera);
                            CAMERA_pan(&cam->camera, 1, 0, real_dt);
                            break;
                        }
                        case SDLK_V: {
                            vsync = !vsync;
                            Renderer_SetVSync(vsync);
                        }
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (hover_tile.x >= 0 && hover_tile.y >= 0 &&
                            hover_tile.x < tilemap->width && hover_tile.y < tilemap->height) {
                            if (building_count < MAX_BUILDINGS) {
                                if (Tilemap_IsTileFree(tilemap, hover_tile.x, hover_tile.y)) {
                                    spawn_boat(hover_tile.x, hover_tile.y);
                                }
                            }
                        }
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION: {
                    SDL_GetMouseState(&mouse_x, &mouse_y);
                    mouse_x *= pixel_ratio;
                    mouse_y *= pixel_ratio;

                    Camera2D_Component *main_camera_component =
                            ss_get(&ecs.cameras, main_camera);
                    if (event.motion.state & SDL_BUTTON_MMASK) {
                        CAMERA_drag(&main_camera_component->camera,
                                    event.motion.xrel, event.motion.yrel, pixel_ratio);
                    }

                    const SDL_FPoint world_position = cam_screen_to_world(
                        &main_camera_component->camera, mouse_x, mouse_y);
                    mouse_tile = Tilemap_ScreenToTile(tilemap, world_position.x, world_position.y);
                    hover_tile = mouse_tile;
                }
                break;
                case SDL_EVENT_MOUSE_WHEEL: {
                    const float zoom_direction = event.wheel.y > 0 ? 1.0f : -1.0f;
                    CAMERA_zoom_apply(&ecs, main_camera, zoom_direction, 12.0f);
                }
                break;
                case SDL_EVENT_WINDOW_RESIZED: {
                    SDL_GetWindowSizeInPixels(window, &screen_width, &screen_height);
                    Renderer_Resize(screen_width, screen_height);
                    Camera2D_Component *main_camera_component =
                            ss_get(&ecs.cameras, main_camera);
                    main_camera_component->camera.viewport.w = screen_width;
                    main_camera_component->camera.viewport.h = screen_height;
                }
                break;
            }
        }

        DebugUI_EndInput();

        // --- Systems ---
        CAMERA_smooth_zoom_system(&ecs, real_dt,
                                  (SDL_FPoint){mouse_x, mouse_y});
        PROF_stop(PROFILER_EVENT_HANDLING);

        // --- Rendering ---
        PROF_start(PROFILER_GPU);
        Renderer_BeginFrame(); // vsync point happens here
        PROF_stop(PROFILER_GPU);

        // Update View-Projection Matrix
        const Camera2D_Component *main_camera_component =
                ss_get(&ecs.cameras, main_camera);
        float scale, offx, offy;
        cam_get_render_params(&main_camera_component->camera, &scale, &offx, &offy);


        float view_proj[16];
        cam_get_view_projection_matrix(&main_camera_component->camera, view_proj);
        Renderer_SetViewProjection(view_proj);

        // Set water animation parameters for shader
        Renderer_SetWaterParams(game_clock.total, wave_speed, wave_amplitude, wave_phase);

        // Render tilemap (water animation handled by shader)
        PROF_start(PROFILER_RENDER_MAP);
        Tilemap_Render(tilemap);
        PROF_stop(PROFILER_RENDER_MAP);

        render_buildings(tilemap, transforms, renderables, building_count, 0, 0);

        // Debug tile highlight (perimeter + beacon on hovered tile)
        render_tile_highlight(tilemap, hover_tile.x, hover_tile.y,
                              (SDL_FColor){0.0f, 1.0f, 1.0f, 1.0f});

        // DEBUG: Draw the tileset texture in the corner to verify sprite pipeline
        Renderer_DrawTextureDebug(tilemap->tileset->texture, 50,
                                  screen_height - 384 - 50, 192, 384);

        if (wireframe_mode) {
            PROF_start(PROFILER_RENDER_WIREFRAMES);
            // FIX: Calculate total vertex count across all meshes (they may differ)
            size_t total_vert_count = 0;
            for (int i = 0; i < building_count; i++) {
                total_vert_count += wireframe_meshes[i].vert_count;
            }
            SDL_Vertex *combined = SDL_malloc(sizeof(SDL_Vertex) * total_vert_count);
            size_t offset = 0;
            for (int i = 0; i < building_count; i++) {
                SDL_memcpy(combined + offset, wireframe_meshes[i].verts,
                           sizeof(SDL_Vertex) * wireframe_meshes[i].vert_count);
                offset += wireframe_meshes[i].vert_count;
            }
            Renderer_DrawGeometry(combined, (int)offset);
            SDL_free(combined);
            PROF_stop(PROFILER_RENDER_WIREFRAMES);
        }

        PROF_start(PROFILER_RENDER_UI);

        float ui_y_pos = 20.0f;

        char hover_tile_info[64];
        snprintf(hover_tile_info, sizeof(hover_tile_info), "Tile: (%d, %d)",
                 hover_tile.x, hover_tile.y);
        TTF_SetTextString(fps_text, hover_tile_info, 0);
        UI_TextWithBackground(fps_text, 10, ui_y_pos);
        ui_y_pos += 40.0f;

        char camera_info[128];
        snprintf(camera_info, sizeof(camera_info),
                 "Camera Pos: (%5.1f, %5.1f) | Zoom: %4.2f",
                 main_camera_component->camera.position.x,
                 main_camera_component->camera.position.y,
                 main_camera_component->camera.zoom);
        TTF_SetTextString(fps_text, camera_info, 0);
        UI_TextWithBackground(fps_text, 10, ui_y_pos);
        ui_y_pos += 40.0f;

        char fps_str[64];
        if (debug_mode) {
            float min, max, avg;
            PROF_getFPS(&min, &avg, &max);
            snprintf(fps_str, sizeof(fps_str), "FPS: min %4.0f  |  avg %4.0f  |  max %4.0f ", min, avg, max);
        } else {
            snprintf(fps_str, sizeof(fps_str), "FPS %4.0f ", 1.0f / real_dt);
        }
        TTF_SetTextString(fps_text, fps_str, 0);
        UI_TextWithBackground(fps_text, 10, ui_y_pos);
        ui_y_pos += 40.0f;

        if (debug_mode) {
            PROF_render((SDL_FPoint){10.0f, ui_y_pos});
        }

        //mouse pos next to mouse
        char mouse_pos_info[64];
        snprintf(mouse_pos_info, sizeof(mouse_pos_info), " %.1f, %.1f ", mouse_x, mouse_y);
        TTF_SetTextString(fps_text, mouse_pos_info, 0);
        UI_TextWithBackground(fps_text, mouse_x + 15.0f, mouse_y + 15.0f);
        UI_Flush();

        PROF_stop(PROFILER_RENDER_UI);

        // --- Debug UI (Nuklear) ---
        PROF_start(PROFILER_NUKLEAR);
        struct nk_context *nk = DebugUI_GetContext();
        const float ui_s = DebugUI_GetScale();
        if (nk && nk_begin(nk, "Debug", nk_rect(50*ui_s, 400*ui_s, 280*ui_s, 400*ui_s),
                          NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                          NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(nk, 25*ui_s, 1);
            nk_labelf(nk, NK_TEXT_LEFT, "Buildings: %d", building_count);
            nk_labelf(nk, NK_TEXT_LEFT, "Hover: (%d, %d)", hover_tile.x, hover_tile.y);
            nk_layout_row_dynamic(nk, 30*ui_s, 2);
            if (nk_button_label(nk, "Spawn 50")) {
                spawn_boats(50);
            }
            if (nk_button_label(nk, "Toggle Wire")) {
                wireframe_mode = !wireframe_mode;
            }
            nk_layout_row_dynamic(nk, 25*ui_s, 1);
            nk_bool dbg = debug_mode;
            nk_checkbox_label(nk, "Debug Mode", &dbg);
            debug_mode = dbg;

            // Sea wave controls
            nk_layout_row_dynamic(nk, 20*ui_s, 1);
            nk_label(nk, "--- Sea Waves ---", NK_TEXT_CENTERED);
            nk_layout_row_dynamic(nk, 20*ui_s, 1);
            nk_labelf(nk, NK_TEXT_LEFT, "Speed: %.2f", wave_speed);
            nk_slider_float(nk, 0.0f, &wave_speed, 2.0f, 0.01f);
            nk_labelf(nk, NK_TEXT_LEFT, "Amplitude: %.2f", wave_amplitude);
            nk_slider_float(nk, 0.0f, &wave_amplitude, 2.0f, 0.01f);
            nk_labelf(nk, NK_TEXT_LEFT, "Phase: %.3f", wave_phase);
            nk_slider_float(nk, 0.0f, &wave_phase, 1.0f, 0.005f);
        }
        nk_end(nk);
        DebugUI_Render();
        PROF_stop(PROFILER_NUKLEAR);

        PROF_start(PROFILER_GPU);
        Renderer_EndFrame();
        PROF_stop(PROFILER_GPU);

        // --- Wait Frame ---
        PROF_start(PROFILER_WAIT_FRAME);
        // const Uint32 wait_time = (Uint32) PROF_getFrameWaitTime();
        // if (wait_time >= 1)
        //     SDL_Delay(wait_time - 1);
        // SDL_Delay(1);

        PROF_stop(PROFILER_WAIT_FRAME);
        PROF_frameEnd();
    }
    return 0;
}

int main(void) {

    if (initialize() == false) {
        return 1;
    }

    const int result = mainLoop();
    if (result != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Application exited with error code: %d\n", result);
        return result;
    }

    clean();

    SDL_Quit();

    return 0;
}
