// What an effect layer's pixels go through (renderer.cpp's RenderEffects): each pass makes a rect of
// one texture from another, a texel at a time.
//
//   PREPARE: the mean of a block of map.x x map.y source texels: a blur's first pass, which makes
//            the texels past `valid` as its tile mode says (clamp, repeated, mirror, decal) and
//            scales the image down for a wide blur.
//   GAUSS:   a Gaussian along an axis, `reach` texels each way, of what is within `valid`.
//
// Destination texel t reads source texels t * map.xy + map.zw (+ the block's, or the kernel's,
// offsets). Positions are texels of the destination, y down.

#ifdef KOMPOZ_SPIRV
#define VERTEX_ID SV_VulkanVertexID
#else
#define VERTEX_ID SV_VertexID
#endif

static const int PREPARE = 0;
static const int GAUSS = 1;

struct Uniforms {
    float4 toClip;  // clip = position * toClip.xy + toClip.zw
    float4 map;     // the source texel a destination texel starts at: t * map.xy + map.zw
    float4 valid;   // the source's own texels: x0, y0, x1, y1
    float4 params;  // the pass, the tile mode, the axis (GAUSS: 0 across, 1 down), sigma
    float4 extra;   // GAUSS: how many texels each way; a source texel's size (zw)
};
[[vk::push_constant]] ConstantBuffer<Uniforms> uniforms;

[[vk::binding(1, 0)]] Sampler2D source;

struct Instance {
    float4 rect : RECT;  // what it draws: x0, y0, x1, y1
};

struct Varyings {
    float4 position : SV_Position;
    float2 texel : TEXEL;  // where in the destination
};

[shader("vertex")]
Varyings vs(Instance instance, uint vertex : VERTEX_ID) {
    static const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    float2 p = lerp(instance.rect.xy, instance.rect.zw, corners[vertex % 6]);
    Varyings out;
    out.position = float4(p * uniforms.toClip.xy + uniforms.toClip.zw, 0, 1);
    out.texel = p;
    return out;
}

// Where the source texel `t` comes from along one axis, the tile mode applied to its own ones,
// [low, high); nothing (-1) for decal past them. (Whole texels: the half keeps the division off
// the whole periods, which a GPU dividing by a reciprocal puts just under them.)
float tiled(float t, float low, float high, int tile) {
    float n = high - low;
    if (t >= low && t < high) return t;
    if (tile == 0) return clamp(t, low, high - 1);
    if (tile == 1) return low + (t - low - n * floor((t - low + 0.5) / n));
    if (tile == 2) {
        float m = t - low - 2 * n * floor((t - low + 0.5) / (2 * n));
        return low + (m < n ? m : 2 * n - 1 - m);
    }
    return -1;
}

float4 fetch(float2 t) {
    int tile = int(uniforms.params.y + 0.5);
    float x = tiled(t.x, uniforms.valid.x, uniforms.valid.z, tile), y = tiled(t.y, uniforms.valid.y, uniforms.valid.w, tile);
    if (x < 0 || y < 0) return float4(0, 0, 0, 0);
    return source.SampleLevel((float2(x, y) + 0.5) * uniforms.extra.zw, 0);
}

[shader("fragment")]
float4 fs(Varyings v) : SV_Target {
    float2 t = floor(v.texel);
    float2 first = t * uniforms.map.xy + uniforms.map.zw;
    int pass = int(uniforms.params.x + 0.5);
    if (pass == PREPARE) {
        int fx = int(uniforms.map.x + 0.5), fy = int(uniforms.map.y + 0.5);
        float4 sum = float4(0, 0, 0, 0);
        for (int j = 0; j < fy; ++j)
            for (int i = 0; i < fx; ++i) sum += fetch(first + float2(i, j));
        return sum / float(fx * fy);
    }
    // A discrete Gaussian: weights exp(-i^2 / 2 sigma^2), adding up to 1.
    float2 axis = uniforms.params.z > 0.5 ? float2(0, 1) : float2(1, 0);
    float sigma = uniforms.params.w;
    int reach = int(uniforms.extra.x + 0.5);
    float4 sum = fetch(first);
    float weights = 1;
    for (int i = 1; i <= reach; ++i) {
        float w = exp(-float(i * i) / (2 * sigma * sigma));
        sum += w * (fetch(first - axis * i) + fetch(first + axis * i));
        weights += 2 * w;
    }
    return sum / weights;
}
