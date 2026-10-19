// Kompoz's 2D primitives, one quad each, drawn by one pipeline: rounded rects filled and stroked,
// lines, arcs, shadows, glyphs, images and layers. Each instance says what it is; the fragment
// shader works out how much of each pixel the shape covers from its distance to the shape's edge,
// so edges are anti-aliased exactly at any scale or rotation.
//
// Positions are pixels of the target, y down; an instance's quad (`bounds`) is moved by its 2x3
// transform (`xform`, `extra.xy`) before it is drawn. Colors come premultiplied.

static const int FILL = 0;         // a rounded rect: shape, radii (tl, tr, br, bl)
static const int STROKE = 1;       // its outline inside it, params.x wide
static const int LINE = 2;         // from shape.xy to shape.zw, params.x wide, square caps
static const int ARC = 3;          // center shape.xy, radius shape.z, params.x wide; from params.y, params.z radians; params.w: round caps
static const int SHADOW = 4;       // a rounded rect's blurred shadow: params.x sigma; params.y: inner (bounds: the rect it is in, uv its radii)
static const int GLYPH = 5;        // the atlas's texels at uv; params.x: a color glyph (drawn as it is, at the color's alpha)
static const int IMAGE = 6;        // an image's texels at uv, at alpha params.x
static const int LAYER = 7;        // a layer's texture: texel (0, 0) at params.xy, uv.zw a texel's size, its pixels within
                                   // params.xy..params.zw; at alpha color.a, cut to the rounded rect (shape, radii), or
                                   // with extra.w = 1 to the path whose coverage mask (texture1, linearly filtered) has
                                   // texel (0, 0) at radii.xy
static const int PATH = 8;         // a path: its coverage mask (texture0) has texel (0, 0) at params.xy; params.z: even-odd
//
// What covers the shape is `color`, or a gradient (`paint`, `gradient`): a fill, a stroke, a path
// and a glyph that takes a color can have one, at the color's alpha.
static const int LINEAR = 1;  // paint.x: the kind; from gradient.xy to gradient.zw
static const int RADIAL = 2;  // round gradient.xy out to gradient.z
static const int SWEEP = 3;   // round gradient.xy, clockwise from 3 o'clock
// paint.y: the tile mode (clamp, repeated, mirror, decal); paint.z: how many stops; paint.w: their
// row in the stops table (texture2): stop i's straight color at texel 2i, its offset at 2i + 1.

// The vertex's index: on SPIR-V (Vulkan, and through it OpenGL ES) without the base vertex that
// D3D's SV_VertexID leaves out, which OpenGL ES doesn't have (draws start at vertex 0 anyway).
#ifdef KOMPOZ_SPIRV
#define VERTEX_ID SV_VulkanVertexID
#else
#define VERTEX_ID SV_VertexID
#endif

struct Uniforms {
    float4 toClip;   // clip = position * toClip.xy + toClip.zw
    float4 texels;   // a texel's size in the coverage mask (xy) and in the stops table (zw)
};
[[vk::push_constant]] ConstantBuffer<Uniforms> uniforms;

[[vk::binding(1, 0)]] Sampler2D texture0;
[[vk::binding(2, 0)]] Sampler2D texture1;
[[vk::binding(3, 0)]] Sampler2D texture2;

struct Instance {
    float4 bounds : BOUNDS;  // the quad: x0, y0, x1, y1
    float4 shape : SHAPE;
    float4 radii : RADII;
    float4 color : COLOR;
    float4 params : PARAMS;
    float4 uv : UV;
    float4 xform : XFORM;  // a, b, c, d: x' = a x + c y + tx, y' = b x + d y + ty
    float4 extra : EXTRA;  // tx, ty, kind, flags
    float4 paint : PAINT;
    float4 gradient : GRADIENT;
};

struct Varyings {
    float4 position : SV_Position;
    float2 local : LOCAL;  // the pixel in the instance's own space
    float2 texcoord : TEXCOORD;
    nointerpolation float4 bounds : BOUNDS;
    nointerpolation float4 shape : SHAPE;
    nointerpolation float4 radii : RADII;
    nointerpolation float4 color : COLOR;
    nointerpolation float4 params : PARAMS;
    nointerpolation float4 uv : UV;
    nointerpolation float kind : KIND;
    nointerpolation float flags : FLAGS;
    nointerpolation float4 paint : PAINT;
    nointerpolation float4 gradient : GRADIENT;
};

[shader("vertex")]
Varyings vs(Instance instance, uint vertex : VERTEX_ID) {
    // Two triangles: (0, 0), (1, 0), (1, 1) and (0, 0), (1, 1), (0, 1).
    static const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    float2 corner = corners[vertex % 6];
    float2 local = lerp(instance.bounds.xy, instance.bounds.zw, corner);
    float2 placed = float2(instance.xform.x * local.x + instance.xform.z * local.y + instance.extra.x,
                           instance.xform.y * local.x + instance.xform.w * local.y + instance.extra.y);
    Varyings out;
    out.position = float4(placed * uniforms.toClip.xy + uniforms.toClip.zw, 0, 1);
    out.local = local;
    out.texcoord = lerp(instance.uv.xy, instance.uv.zw, corner);
    out.bounds = instance.bounds;
    out.shape = instance.shape;
    out.radii = instance.radii;
    out.color = instance.color;
    out.params = instance.params;
    out.uv = instance.uv;
    out.kind = instance.extra.z;
    out.flags = instance.extra.w;
    out.paint = instance.paint;
    out.gradient = instance.gradient;
    return out;
}

// The signed distance from p to a rounded rect (negative inside), each corner its own radius.
float roundRectDistance(float2 p, float4 rect, float4 radii) {
    float2 center = (rect.xy + rect.zw) * 0.5, halfSize = (rect.zw - rect.xy) * 0.5;
    float2 q = p - center;
    float r = q.x < 0 ? (q.y < 0 ? radii.x : radii.w) : (q.y < 0 ? radii.y : radii.z);
    float2 d = abs(q) - halfSize + r;
    return min(max(d.x, d.y), 0) + length(max(d, 0)) - r;
}

// How much of the pixel a distance (in the instance's pixels) covers: `aa` is a device pixel's
// size there.
float coverage(float distance, float aa) {
    return saturate(0.5 - distance / aa);
}

// How much of the pixel at x (device pixels across a band from -h to h) the band covers.
float bandCoverage(float x, float h) {
    return saturate(min(x + 0.5, h) - max(x - 0.5, -h));
}

// A path's coverage from its mask's sum at the pixel (see shaders/path.slang): by the non-zero
// rule, or the even-odd one.
float pathCoverage(float sum, bool evenOdd) {
    sum = abs(sum);
    if (!evenOdd) return min(sum, 1);
    float m = sum - 2 * floor(sum / 2);
    return m <= 1 ? m : 2 - m;
}

// Where a mask texel is sampled: its middle (with nearest filtering, texel for pixel).
float2 maskTexcoord(float2 texel) {
    return (floor(texel) + 0.5) * uniforms.texels.xy;
}

// The paint at p: `color`, or the gradient's color there (as Brush::colorAt works it out: the
// stops' straight colors mixed, then premultiplied) at the color's alpha. (Written without a
// return in a branch after the first: llvmpipe on LLVM 20 crashes compiling the tile modes'
// branches with one.)
float4 paintAt(float2 p, float4 color, float4 paint, float4 g) {
    int kind = int(paint.x + 0.5);
    if (kind == 0) return color;
    const float pi = 3.141592653589793;
    float t = 0;
    if (kind == LINEAR) {
        float2 d = g.zw - g.xy;
        float len2 = dot(d, d);
        t = len2 > 0 ? dot(p - g.xy, d) / len2 : 0;
    } else if (kind == RADIAL) {
        t = g.z > 0 ? length(p - g.xy) / g.z : 0;
    } else {
        float a = atan2(p.y - g.y, p.x - g.x);
        if (a < 0) a += 2 * pi;
        t = a / (2 * pi);
    }
    int tile = int(paint.y + 0.5);
    float shown = tile == 3 && (t < 0 || t > 1) ? 0 : 1;  // decal: nothing past its ends
    if (tile == 0 || tile == 3) {
        t = saturate(t);
    } else if (tile == 1) {
        t -= floor(t);
    } else {
        t = abs(t) - 2 * floor(abs(t) / 2);
        t = t > 1 ? 2 - t : t;
    }
    // The stop t falls after and the one it falls before (straight colors), mixed.
    int count = int(paint.z + 0.5);
    float v = (paint.w + 0.5) * uniforms.texels.w;
    float4 previous = texture2.SampleLevel(float2(0.5 * uniforms.texels.z, v), 0);
    float before = texture2.SampleLevel(float2(1.5 * uniforms.texels.z, v), 0).x;
    float4 straight = previous;
    bool found = t <= before;
    for (int i = 1; i < count; ++i) {
        float4 next = texture2.SampleLevel(float2((2 * i + 0.5) * uniforms.texels.z, v), 0);
        float offset = texture2.SampleLevel(float2((2 * i + 1.5) * uniforms.texels.z, v), 0).x;
        if (!found && t <= offset) {
            float span = offset - before;
            straight = lerp(previous, next, span > 0 ? (t - before) / span : 1);
            found = true;
        }
        previous = next;
        before = offset;
    }
    straight = found ? straight : previous;  // past the last stop: its color
    return float4(straight.rgb * straight.a, straight.a) * color.a * shown;
}

// A Gaussian's integral, approximated (Abramowitz and Stegun 7.1.25).
float2 erf2(float2 x) {
    float2 s = sign(x), a = abs(x);
    x = 1 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

float gaussian(float x, float sigma) {
    const float pi = 3.141592653589793;
    return exp(-(x * x) / (2 * sigma * sigma)) / (sqrt(2 * pi) * sigma);
}

// A blurred rounded rect's coverage at p: exact across, four samples down (Evan Wallace's).
float blurredRoundRect(float2 p, float4 rect, float4 radii, float sigma) {
    float2 center = (rect.xy + rect.zw) * 0.5, halfSize = (rect.zw - rect.xy) * 0.5;
    float2 q = p - center;
    float corner = q.x < 0 ? (q.y < 0 ? radii.x : radii.w) : (q.y < 0 ? radii.y : radii.z);
    float low = q.y - halfSize.y, high = q.y + halfSize.y;
    float start = clamp(-3 * sigma, low, high), end = clamp(3 * sigma, low, high);
    float step = (end - start) / 4, y = start + step * 0.5, value = 0;
    for (int i = 0; i < 4; ++i) {
        float dy = q.y - y;
        float delta = min(halfSize.y - corner - abs(dy), 0);
        float curved = halfSize.x - corner + sqrt(max(0, corner * corner - delta * delta));
        float2 integral = 0.5 + 0.5 * erf2((q.x + float2(-curved, curved)) * (sqrt(0.5) / sigma));
        value += (integral.y - integral.x) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

// The distance to an arc's ring from `start`, `sweep` radians round (round or flat ends).
float arcDistance(float2 q, float radius, float halfSize, float start, float sweep, bool roundCaps) {
    float aperture = abs(sweep) * 0.5;
    float ring = abs(length(q) - radius) - halfSize;
    if (aperture >= 3.14159) return ring;
    float middle = start + sweep * 0.5;
    float c = cos(-middle), s = sin(-middle);
    float2 p = float2(c * q.x - s * q.y, s * q.x + c * q.y);
    p.y = abs(p.y);  // symmetric about the middle
    float2 end = float2(cos(aperture), sin(aperture));
    if (atan2(p.y, p.x) <= aperture) return ring;
    float2 cap = end * radius;
    if (roundCaps) return length(p - cap) - halfSize;
    float2 outward = float2(-end.y, end.x);  // past the end, along the circle
    float along = dot(p - cap, outward), across = abs(dot(p - cap, end)) - halfSize;
    return across <= 0 ? along : length(float2(max(along, 0), across));
}

[shader("fragment")]
float4 fs(Varyings v) : SV_Target {
    int kind = int(v.kind + 0.5);
    float4 color = paintAt(v.local, v.color, v.paint, v.gradient);
    float aa = 0.5 * (length(ddx(v.local)) + length(ddy(v.local)));
    float alpha = 0;
    if (kind == FILL) {
        alpha = coverage(roundRectDistance(v.local, v.shape, v.radii), aa);
    } else if (kind == STROKE) {
        float w = v.params.x;
        float4 inner = float4(v.shape.xy + w, v.shape.zw - w);
        float outside = coverage(roundRectDistance(v.local, v.shape, v.radii), aa);
        float inside = coverage(roundRectDistance(v.local, inner, max(v.radii - w, 0)), aa);
        alpha = saturate(outside - inside);
    } else if (kind == LINE) {
        float2 a = v.shape.xy, b = v.shape.zw;
        float len = length(b - a);
        float2 dir = len > 0 ? (b - a) / len : float2(1, 0);
        float2 d = v.local - a;
        float along = dot(d, dir) - len * 0.5, across = dot(d, float2(-dir.y, dir.x));
        float h = v.params.x;
        alpha = bandCoverage(across / aa, h / aa) * bandCoverage(along / aa, (len * 0.5 + h) / aa);
    } else if (kind == ARC) {
        alpha = coverage(arcDistance(v.local - v.shape.xy, v.shape.z, v.params.x, v.params.y, v.params.z, v.params.w > 0.5), aa);
    } else if (kind == SHADOW) {
        float sigma = v.params.x;
        bool inner = v.params.y > 0.5;
        float blurred = sigma > 0.01 ? blurredRoundRect(v.local, v.shape, v.radii, sigma)
                                     : coverage(roundRectDistance(v.local, v.shape, v.radii), aa);
        if (inner) {  // inside the rect (bounds, its radii in uv), where the blurred hole isn't
            alpha = (1 - blurred) * coverage(roundRectDistance(v.local, v.bounds, v.uv), aa);
        } else {
            alpha = blurred;
        }
    } else if (kind == GLYPH) {
        float4 texel = texture0.Sample(v.texcoord);
        if (v.params.x > 0.5) return float4(texel.rgb * texel.a, texel.a) * v.color.a;
        return color * texel.a;
    } else if (kind == IMAGE) {
        float4 texel = texture0.Sample(v.texcoord);
        return float4(texel.rgb * texel.a, texel.a) * v.params.x;
    } else if (kind == LAYER) {
        float2 texel = clamp(v.local, v.params.xy + 0.5, v.params.zw - 0.5);
        float4 color = texture0.Sample((texel - v.params.xy) * v.uv.zw);
        // (Filtered where it is: a turned or scaled layer's pixels fall between the mask's texels.)
        float cut = v.flags > 0.5 ? pathCoverage(texture1.SampleLevel((v.local - v.radii.xy) * uniforms.texels.xy, 0).r, false)
                                  : coverage(roundRectDistance(v.local, v.shape, v.radii), aa);
        return color * v.color.a * cut;
    } else if (kind == PATH) {
        alpha = pathCoverage(texture0.SampleLevel(maskTexcoord(v.local - v.params.xy), 0).r, v.params.z > 0.5);
    }
    return color * alpha;
}
