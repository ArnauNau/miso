#include <metal_stdlib>
using namespace metal;

// Must match C struct SpriteInstance exactly (48 bytes):
// typedef struct {
//   float x, y, z, flags;     // 16 bytes - position + flags (1.0 = water)
//   float w, h, tile_x, tile_y; // 16 bytes - size + tile grid position
//   float u, v, uw, vh;       // 16 bytes - UV coordinates
// } SpriteInstance;
struct InstanceData {
    float4 position;      // x, y, z, flags (flags: 1.0 = water, 0.0 = normal)
    float4 size;          // w, h, tile_x, tile_y
    float4 texRegion;     // u, v, uw, vh
};

struct Uniforms {
    float4x4 viewProjection;
    float4 waterParams;   // x=time, y=speed, z=amplitude, w=phase
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut vertex_main(
    uint vertexID [[vertex_id]],
    uint instanceID [[instance_id]],
    constant Uniforms &uniforms [[buffer(0)]],
    const device InstanceData *instances [[buffer(1)]]
) {
    InstanceData instance = instances[instanceID];

    // Standard quad vertices (0,0 to 1,1)
    float2 quadVerts[6] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(0.0, 1.0),
        float2(1.0, 0.0),
        float2(1.0, 1.0)
    };

    float2 vertPos = quadVerts[vertexID];

    // Extract instance data
    float2 worldPos = instance.position.xy;
    float depth = instance.position.z;
    float isWater = instance.position.w;  // 1.0 = water, 0.0 = normal
    float2 spriteSize = instance.size.xy;
    float tileX = instance.size.z;
    float tileY = instance.size.w;

    // Water animation (GPU-side)
    if (isWater > 0.5) {
        float time = uniforms.waterParams.x;
        float speed = uniforms.waterParams.y;
        float amplitude = uniforms.waterParams.z;
        float phase = uniforms.waterParams.w;

        // Isometric tile height (half of tile pixel height)
        float isoH = spriteSize.y / 2.0;

        // Wave calculation (matches original CPU implementation)
        float tilePhase = (tileX + tileY) * phase;
        float adjustedAmplitude = (isoH / 2.0) * amplitude;
        float waveOffsetCompensation = ((isoH / 2.0) * amplitude) + (isoH / 2.0);
        float waveTime = time * M_PI_F * 2.0 * speed;
        float waveOffset = sin(waveTime + tilePhase) * adjustedAmplitude - waveOffsetCompensation;

        // Apply wave offset to Y position (negative because wave moves tile up/down)
        worldPos.y -= waveOffset;
    }

    // Scale position by instance size
    worldPos = worldPos + (vertPos * spriteSize);

    // Transform to clip space
    float4 pos = uniforms.viewProjection * float4(worldPos, depth, 1.0);

    // Calculate texture coordinates
    float2 texCoord = instance.texRegion.xy + (vertPos * instance.texRegion.zw);

    VertexOut out;
    out.position = pos;
    out.texCoord = texCoord;
    return out;
}

fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    texture2d<float> spriteTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]]
) {
    float4 color = spriteTexture.sample(textureSampler, in.texCoord);
    if (color.a < 0.1) {
        discard_fragment();
    }
    return color;
}
