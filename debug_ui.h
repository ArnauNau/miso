#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include <SDL3/SDL.h>
#include <stdbool.h>

// Forward declaration - actual nk_context defined in nuklear.h
struct nk_context;

// Initialize debug UI with a TTF font file
// Call after Renderer_Init()
bool DebugUI_Init(const char *font_path, float font_size);

// Shutdown and cleanup
void DebugUI_Shutdown(void);

// Handle SDL events. Returns true if event was consumed by UI.
// Call this before your own event handling.
bool DebugUI_HandleEvent(SDL_Event *evt);

// Call at start of frame before defining UI
void DebugUI_BeginInput(void);

// Call after processing all events
void DebugUI_EndInput(void);

// Get Nuklear context for defining UI
struct nk_context* DebugUI_GetContext(void);

// Get the UI scale factor (pixel density)
float DebugUI_GetScale(void);

// Render the UI. Call after UI_Flush() but before Renderer_EndFrame()
void DebugUI_Render(void);

#endif // DEBUG_UI_H
