// What an effect layer's pixels go through (renderer.cpp's RenderEffects): each pass makes a rect of
// one texture from another, a texel at a time.
//
//   PREPARE: the mean of a block of map.x x map.y source texels: a blur's first pass, which makes
//            the texels past `valid` as its tile mode says (clamp, repeated, mirror, decal) and
//            scales the image down for a wide blur.
//   GAUSS:   a Gaussian along an axis, `reach` texels each way, of what is within `valid`.
//   PROGRESSIVE: Compose's progressive blur along an axis (its BlurShaders): each pixel by its
//            own radius, from where it lies along a line or out from a center between stops (the
//            table's row), its own Gaussian of sigma r / 2 px out to r px, taps in pairs between
//            texels (linearly filtered); the taps past the layer's bounds (`valid`, on the axis)
//            left out, and counted (decal) or not (otherwise: the others weigh more).
//
// Destination texel t reads source texels t * map.xy + map.zw (+ the block's, or the kernel's,
// offsets); PROGRESSIVE's points p of the destination are p * map.xy + map.zw in the source.
// Positions are texels of the destination, y down.

#ifdef KOMPOZ_SPIRV
#define VERTEX_ID SV_VulkanVertexID
#else
#define VERTEX_ID SV_VertexID
#endif

static const int PREPARE = 0;
static const int GAUSS = 1;
static const int PROGRESSIVE = 2;

struct Uniforms {
    float4 toClip;    // clip = position * toClip.xy + toClip.zw
    float4 map;       // the source texel a destination texel starts at: t * map.xy + map.zw
    float4 valid;     // the source's own texels: x0, y0, x1, y1 (PROGRESSIVE: the bounds, in the destination's)
    float4 params;    // the pass, the tile mode, the axis (0 across, 1 down), sigma (PROGRESSIVE: the widest radius, px)
    float4 extra;     // GAUSS: how many texels each way (PROGRESSIVE: texels a px); a source texel's size (zw)
    float4 mask;      // PROGRESSIVE: along a line (1) or out from a center (2); the stops' row, how many
    float4 geometry;  // the line's start and end, or the center and how far out (texels from `origin`)
    float4 origin;    // where the layer's top left is in the destination; a table texel's size (zw)
};
[[vk::push_constant]] ConstantBuffer<Uniforms> uniforms;

[[vk::binding(1, 0)]] Sampler2D source;
[[vk::binding(2, 0)]] Sampler2D table;  // PROGRESSIVE's stops: stop i at texel i (fraction, radius over the widest)

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

// The radius over the widest at `fraction` along: between the stops around it, the first's before
// them, the last's after.
float intensity(float fraction) {
    int n = int(uniforms.mask.z + 0.5);
    float row = (uniforms.mask.y + 0.5) * uniforms.origin.w;
    float2 a = table.SampleLevel(float2(0.5 * uniforms.origin.z, row), 0).xy;
    if (fraction <= a.x) return a.y;
    for (int i = 1; i < n; ++i) {
        float2 b = table.SampleLevel(float2((i + 0.5) * uniforms.origin.z, row), 0).xy;
        if (fraction >= a.x && fraction <= b.x) return lerp(a.y, b.y, saturate((fraction - a.x) / max(b.x - a.x, 0.0001)));
        a = b;
    }
    return a.y;
}

// Where the pixel `c` lies along the blur (0..1): left and above the layer as at its edge.
float fraction(float2 c) {
    float2 p = max(c - uniforms.origin.xy, 0);
    float4 g = uniforms.geometry;
    if (uniforms.mask.x < 1.5) {
        float2 ba = g.zw - g.xy;
        float l = dot(ba, ba);
        return l > 0 ? saturate(dot(p - g.xy, ba) / l) : 0;
    }
    return g.z > 0 ? saturate(distance(p, g.xy) / g.z) : 0;
}

float4 content(float2 p) { return source.SampleLevel((p * uniforms.map.xy + uniforms.map.zw) * uniforms.extra.zw, 0); }

float gaussian(float x, float sigma) { return exp(-(x * x) / (2 * sigma * sigma)); }

float4 progressive(float2 c) {
    float2 axis = uniforms.params.z > 0.5 ? float2(0, 1) : float2(1, 0);
    float low = dot(uniforms.valid.xy, axis), high = dot(uniforms.valid.zw, axis);
    float unbounded = uniforms.params.y > 2.5 ? 1 : 0;  // decal
    float unit = uniforms.extra.x;
    float radius = uniforms.params.w * clamp(intensity(fraction(c)), 0, 1);
    float r = floor(radius);
    float4 sum = content(c);
    if (r < 1) return sum;
    float sigma = max(radius / 2, 1), weights = 1;
    for (float i = 1; i < 150; i += 2) {
        if (i >= r) break;
        float low1 = gaussian(i, sigma), high1 = gaussian(i + 1, sigma), w = low1 + high1;
        float2 offset = axis * ((i + high1 / w) * unit);
        for (int side = -1; side <= 1; side += 2) {
            float2 p = c + side * offset;
            float along = dot(p, axis), in = step(low, along) * (1 - step(high, along));
            weights += w * max(in, unbounded);
            if (in > 0) sum += w * content(p);
        }
    }
    float odd = (r - 2 * floor(r / 2)) * (1 - step(150, r)), w = gaussian(r, sigma) * odd;
    for (int side = -1; side <= 1; side += 2) {
        float2 p = c + side * axis * (r * unit);
        float along = dot(p, axis), in = step(low, along) * (1 - step(high, along));
        weights += w * max(in, unbounded);
        if (in > 0) sum += w * content(p);
    }
    return sum / weights;
}

[shader("fragment")]
float4 fs(Varyings v) : SV_Target {
    float2 t = floor(v.texel);
    if (int(uniforms.params.x + 0.5) == PROGRESSIVE) return progressive(t + 0.5);
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
