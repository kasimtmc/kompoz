// Kompoz's 2D primitives, one quad each, drawn by one pipeline: rounded rects filled and stroked,
// lines, arcs, shadows, glyphs, images and layers. Each instance says what it is; the fragment
// shader works out how much of each pixel the shape covers from its distance to the shape's edge,
// so edges are anti-aliased exactly at any scale or rotation.
//
// Positions are pixels of the target, y down; an instance's quad (`bounds`) is moved by its
// projective transform (`xform`, `extra.xy`, `projection`) before it is drawn, and its shape is
// worked out where it is before that (`local`), so it stays exact however it is turned, stretched
// or seen in perspective. Colors come premultiplied.

static const int FILL = 0;         // a rounded rect: shape, radii (tl, tr, br, bl)
static const int STROKE = 1;       // its outline inside it, params.x wide
static const int LINE = 2;         // from shape.xy to shape.zw, params.x wide, square caps
static const int ARC = 3;          // center shape.xy, radius shape.z, params.x wide; from params.y, params.z radians; params.w: round caps
static const int SHADOW = 4;       // a rounded rect's blurred shadow: params.x sigma; params.y: inner (bounds: the rect it is in, uv its radii)
static const int GLYPH = 5;        // the atlas's texels at uv; params.x: a color glyph (drawn as it is, at the color's alpha)
static const int IMAGE = 6;        // an image's texels at uv, at alpha params.x
                                   // (both: uv spans `shape`; with flags & 2 the quad reaches past it, its edges
                                   // anti-aliased, the texels sampled no further than params.zw inside uv's)
static const int LAYER = 7;        // a layer's texture: texel (0, 0) at params.xy, uv.zw a texel's size, its pixels within
                                   // params.xy..params.zw; at alpha color.a, cut to the rounded rect (shape, radii), or
                                   // with flags & 1 to the path whose coverage mask (texture1, linearly filtered) has
                                   // texel (0, 0) at radii.xy
static const int PATH = 8;         // a path: its coverage mask (texture0) has texel (0, 0) at params.xy; params.z: even-odd;
                                   // drawn in the target's pixels, with flags & 4 its paint where the pixel is in its
                                   // own space (radii, uv and shape: the rows of the map there)
//
// What covers the shape is `color`, or a gradient (`paint`, `gradient`): a fill, a stroke, a path
// and a glyph that takes a color can have one, at the color's alpha.
static const int LINEAR = 1;  // paint.x: the kind; from gradient.xy to gradient.zw
static const int RADIAL = 2;  // round gradient.xy out to gradient.z
static const int SWEEP = 3;   // round gradient.xy, clockwise from 3 o'clock
// paint.y: the tile mode (clamp, repeated, mirror, decal); paint.z: how many stops; paint.w: their
// row in the table (texture2): stop i's straight color at texel 2i, its offset at 2i + 1.
//
// Its coverage is cut by the clips it is drawn in (filter.z of them, from row filter.w of the
// table, six texels each: see clipCoverage). Then its color goes through color filters (filter.x
// of them, from row filter.y, six texels each, innermost first) and onto the target by the batch's
// blend mode (uniforms.blend):
// by the blend unit with the coverage multiplied in, or (a mode it can't do) worked out here from
// a copy of the target (texture3) and mixed with it by the coverage.

// The vertex's index: on SPIR-V (Vulkan, and through it OpenGL ES) without the base vertex that
// D3D's SV_VertexID leaves out, which OpenGL ES doesn't have (draws start at vertex 0 anyway).
#ifdef KOMPOZ_SPIRV
#define VERTEX_ID SV_VulkanVertexID
#else
#define VERTEX_ID SV_VertexID
#endif

struct Uniforms {
    float4 toClip;   // clip = position * toClip.xy + toClip.zw
    float4 texels;   // a texel's size in the coverage mask (xy) and in the table (zw)
    float4 blend;    // the blend mode (x), and how the coverage goes in (y: see finish)
};
[[vk::push_constant]] ConstantBuffer<Uniforms> uniforms;

[[vk::binding(1, 0)]] Sampler2D texture0;
[[vk::binding(2, 0)]] Sampler2D texture1;
[[vk::binding(3, 0)]] Sampler2D texture2;
[[vk::binding(4, 0)]] Sampler2D texture3;

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
    float4 filter : FILTER;  // how many color filters, their row in the table; how many clips, their row
    float4 projection : PROJECTION;  // g, h, i: the transform's w = g x + h y + i
};

struct Varyings {
    float4 position : SV_Position;
    float2 local : LOCAL;  // the pixel in the instance's own space
    float w : DEPTH_W;     // the transform's w there (1 unless in perspective)
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
    nointerpolation float4 filter : FILTER;
};

[shader("vertex")]
Varyings vs(Instance instance, uint vertex : VERTEX_ID) {
    // Two triangles: (0, 0), (1, 0), (1, 1) and (0, 0), (1, 1), (0, 1).
    static const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    float2 corner = corners[vertex % 6];
    float2 local = lerp(instance.bounds.xy, instance.bounds.zw, corner);
    float3 placed = float3(instance.xform.x * local.x + instance.xform.z * local.y + instance.extra.x,
                           instance.xform.y * local.x + instance.xform.w * local.y + instance.extra.y,
                           instance.projection.x * local.x + instance.projection.y * local.y + instance.projection.z);
    Varyings out;
    out.position = float4(placed.xy * uniforms.toClip.xy + placed.z * uniforms.toClip.zw, 0, placed.z);
    out.local = local;
    out.w = placed.z;
    int kind = int(instance.extra.z + 0.5);
    float2 across = (kind == GLYPH || kind == IMAGE) ? (local - instance.shape.xy) / (instance.shape.zw - instance.shape.xy) : corner;
    out.texcoord = lerp(instance.uv.xy, instance.uv.zw, across);
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
    out.filter = instance.filter;
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

// How much a distance changes over a device pixel across and down, the distance growing along
// `n` (in the instance's own units) and a pixel's steps across and down being dx and dy there.
float2 pixelSteps(float2 n, float2 dx, float2 dy) {
    return float2(dot(dx, n), dot(dy, n));
}

// How much of a device pixel the inside of a straight edge covers: the pixel's box cut by the
// line, exactly. `d` is the distance to the edge (negative inside), `g` how much it changes
// across and down the pixel.
float edgeCoverage(float d, float2 g) {
    float len = max(length(g), 1e-6);
    float2 n = abs(g) / len;
    float a = max(n.x, n.y), b = min(n.x, n.y);  // the box is a + b long across the edge
    float u = clamp(0.5 * (a + b) - d / len, 0, a + b);  // how far into the box the inside reaches
    float twice = max(2 * a * b, 1e-12);
    // A corner of the box, then a band across it, then all but the opposite corner.
    return u <= b ? u * u / twice : (u <= a ? (u - 0.5 * b) / a : 1 - (a + b - u) * (a + b - u) / twice);
}

// How much of the pixel at p a rounded rect covers: its straight edges each cut the pixel's box
// exactly (a corner as the two edges' product, which is exact while it is square to the screen),
// a rounded corner by the circle's edge where the pixel is.
float roundRectCoverage(float2 p, float4 rect, float4 radii, float2 dx, float2 dy) {
    float2 gx = float2(dx.x, dy.x), gy = float2(dx.y, dy.y);  // how x and y change across and down
    float across = edgeCoverage(rect.x - p.x, -gx) + edgeCoverage(p.x - rect.z, gx) - 1;
    float down = edgeCoverage(rect.y - p.y, -gy) + edgeCoverage(p.y - rect.w, gy) - 1;
    float straight = saturate(across) * saturate(down);
    float2 center = (rect.xy + rect.zw) * 0.5, halfSize = (rect.zw - rect.xy) * 0.5;
    float2 q = p - center;
    float r = q.x < 0 ? (q.y < 0 ? radii.x : radii.w) : (q.y < 0 ? radii.y : radii.z);
    float2 corner = abs(q) - halfSize + r;  // from the corner circle's middle
    float len = length(corner);
    float2 n = (len > 0 ? corner / len : float2(0.7071, 0.7071)) * float2(q.x < 0 ? -1 : 1, q.y < 0 ? -1 : 1);
    float curved = edgeCoverage(len - r, pixelSteps(n, dx, dy));
    return r > 0 && corner.x > 0 && corner.y > 0 ? min(curved, straight) : straight;
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

// --- Blend modes (BlendMode's order), as blend() in graphics.cpp works them out ---------------

float lum(float3 c) { return dot(c, float3(0.3, 0.59, 0.11)); }
float3 clipColor(float3 c) {
    float l = lum(c), n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));
    float3 out = c;
    if (n < 0 && l - n > 0) out = l + (out - l) * l / (l - n);
    if (x > 1 && x - l > 0) out = l + (out - l) * (1 - l) / (x - l);
    return out;
}
float3 setLum(float3 c, float l) { return clipColor(c + (l - lum(c))); }
float sat(float3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
float3 setSat(float3 c, float s) {
    float n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));
    return x > n ? (c - n) * s / (x - n) : float3(0, 0, 0);
}
float hardLight(float s, float b) { return s <= 0.5 ? b * 2 * s : b + (2 * s - 1) - b * (2 * s - 1); }
float separable(int mode, float cs, float cb) {
    float r = cs;
    if (mode == 6) r = cs * cb;                    // Multiply
    else if (mode == 7) r = cs + cb - cs * cb;     // Screen
    else if (mode == 8) r = min(cs, cb);           // Darken
    else if (mode == 9) r = max(cs, cb);           // Lighten
    else if (mode == 18) r = hardLight(cb, cs);    // Overlay
    else if (mode == 19) r = cb <= 0 ? 0 : (cs >= 1 ? 1 : min(1, cb / (1 - cs)));  // ColorDodge
    else if (mode == 20) r = cb >= 1 ? 1 : (cs <= 0 ? 0 : 1 - min(1, (1 - cb) / cs));  // ColorBurn
    else if (mode == 21) r = hardLight(cs, cb);    // Hardlight
    else if (mode == 22) {                         // Softlight
        float d = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb : sqrt(cb);
        r = cs <= 0.5 ? cb - (1 - 2 * cs) * cb * (1 - cb) : cb + (2 * cs - 1) * (d - cb);
    } else if (mode == 23) r = abs(cs - cb);       // Difference
    else if (mode == 24) r = cs + cb - 2 * cs * cb;  // Exclusion
    return r;
}

// s onto d with `mode`, both premultiplied (as the result).
float4 blendColors(float4 s, float4 d, int mode) {
    float sa = s.a, da = d.a;
    float3 cs = sa > 0 ? s.rgb / sa : float3(0, 0, 0), cb = da > 0 ? d.rgb / da : float3(0, 0, 0);
    // Porter-Duff: s fs + d fd.
    float fs = -1, fd = 0;
    if (mode == 11) { fs = 0; fd = 0; }               // Clear
    else if (mode == 0) { fs = 1; fd = 0; }           // Src
    else if (mode == 12) { fs = 0; fd = 1; }          // Dst
    else if (mode == 1) { fs = 1; fd = 1 - sa; }      // SrcOver
    else if (mode == 13) { fs = 1 - da; fd = 1; }     // DstOver
    else if (mode == 2) { fs = da; fd = 0; }          // SrcIn
    else if (mode == 4) { fs = 0; fd = sa; }          // DstIn
    else if (mode == 14) { fs = 1 - da; fd = 0; }     // SrcOut
    else if (mode == 15) { fs = 0; fd = 1 - sa; }     // DstOut
    else if (mode == 3) { fs = da; fd = 1 - sa; }     // SrcAtop
    else if (mode == 16) { fs = 1 - da; fd = sa; }    // DstAtop
    else if (mode == 17) { fs = 1 - da; fd = 1 - sa; }  // Xor
    float4 out;
    if (fs >= 0) {
        out = s * fs + d * fd;
    } else if (mode == 10) {  // Plus
        out = min(s + d, float4(1, 1, 1, 1));
    } else if (mode == 5) {   // Modulate
        out = s * d;
    } else {
        float3 b;
        if (mode == 25) b = setLum(setSat(cs, sat(cb)), lum(cb));       // Hue
        else if (mode == 26) b = setLum(setSat(cb, sat(cs)), lum(cb));  // Saturation
        else if (mode == 27) b = setLum(cs, lum(cb));                   // Color
        else if (mode == 28) b = setLum(cb, lum(cs));                   // Luminosity
        else b = float3(separable(mode, cs.r, cb.r), separable(mode, cs.g, cb.g), separable(mode, cs.b, cb.b));
        out = float4(s.rgb * (1 - da) + d.rgb * (1 - sa) + sa * da * b, sa + da - sa * da);
    }
    return out;
}

// --- Color filters, as ColorFilter::apply works them out --------------------------------------

float4 tableTexel(float x, float row) {
    return texture2.SampleLevel(float2((x + 0.5) * uniforms.texels.z, (row + 0.5) * uniforms.texels.w), 0);
}

// A premultiplied color through the filters (each on the straight color).
float4 applyFilters(float4 color, float4 filter) {
    int count = int(filter.x + 0.5);
    float4 c = color.a > 0 ? float4(color.rgb / color.a, color.a) : float4(0, 0, 0, 0);
    for (int i = 0; i < count; ++i) {
        float x = float(i * 6);
        float4 head = tableTexel(x, filter.y);
        int kind = int(head.x + 0.5);
        float4 a = tableTexel(x + 1, filter.y), b = tableTexel(x + 2, filter.y);
        if (kind == 1) {  // a matrix: rows in texels 1..4, the added column in 5
            float4 r2 = tableTexel(x + 3, filter.y), r3 = tableTexel(x + 4, filter.y), added = tableTexel(x + 5, filter.y);
            c = saturate(float4(dot(a, c), dot(b, c), dot(r2, c), dot(r3, c)) + added);
        } else if (kind == 2) {  // lighting: multiply (texel 1), add (texel 2); alpha as it is
            c = float4(saturate(c.rgb * a.rgb + b.rgb), c.a);
        } else {  // a tint (texel 1, straight) by its blend mode
            float4 tinted = blendColors(float4(a.rgb * a.a, a.a), float4(c.rgb * c.a, c.a), int(head.y + 0.5));
            c = tinted.a > 0 ? float4(saturate(tinted.rgb / tinted.a), min(tinted.a, 1)) : float4(0, 0, 0, 0);
        }
    }
    return count > 0 ? float4(c.rgb * c.a, c.a) : color;
}

// What goes to the target: a premultiplied color covering `cover` of the pixel, by the batch's
// mode (uniforms.blend.y): 0 multiplied by the coverage, for the blend unit; 1 (Modulate) and 2
// (DstIn) mixed towards what changes nothing; 3 (Clear) the coverage alone; 4 the mode worked out
// here against the target's copy and mixed with it.
float4 finish(float4 src, float cover, float2 pixel) {
    int how = int(uniforms.blend.y + 0.5);
    float4 out = src * cover;
    if (how == 1) {
        out = lerp(float4(1, 1, 1, 1), src, cover);
    } else if (how == 2) {
        out = float4(0, 0, 0, lerp(1, src.a, cover));
    } else if (how == 3) {
        out = float4(0, 0, 0, cover);
    } else if (how == 4) {
        float4 dst = texture3.SampleLevel(pixel * float2(uniforms.toClip.x, abs(uniforms.toClip.y)) * 0.5, 0);
        out = lerp(dst, blendColors(src, dst, int(uniforms.blend.x + 0.5)), cover);
    }
    return out;
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

// Whether q is past an arc's ends (w: 1), `sweep` radians round from `start`, and if so the
// distance to its cap there (round or flat) and the way it grows (xy, z).
float4 arcCap(float2 q, float radius, float halfSize, float start, float sweep, bool roundCaps) {
    float aperture = abs(sweep) * 0.5;
    float4 field = float4(0, 1, 0, 0);
    float middle = start + sweep * 0.5;
    float c = cos(-middle), s = sin(-middle);
    float2 p = float2(c * q.x - s * q.y, s * q.x + c * q.y);
    float mirror = p.y < 0 ? -1 : 1;
    p.y = abs(p.y);  // symmetric about the middle
    float2 end = float2(cos(aperture), sin(aperture));
    if (aperture < 3.14159 && atan2(p.y, p.x) > aperture) {  // past an end: its cap
        float2 cap = end * radius, v = p - cap;
        float2 outward = float2(-end.y, end.x);  // past the end, along the circle
        float d = 0;
        float2 n = outward;
        if (roundCaps) {
            float l = length(v);
            d = l - halfSize;
            n = l > 0 ? v / l : outward;
        } else {
            float along = dot(v, outward), toward = dot(v, end), across = abs(toward) - halfSize;
            float2 beyond = float2(max(along, 0), max(across, 0));
            float l = length(beyond);
            d = across <= 0 ? along : l;
            n = across <= 0 || l <= 0 ? outward : (outward * beyond.x + end * (toward < 0 ? -1 : 1) * beyond.y) / l;
        }
        n.y *= mirror;
        field = float4(d, c * n.x + s * n.y, -s * n.x + c * n.y, 1);  // turned back
    }
    return field;
}

// The clips a primitive is drawn in, from row `row` of the table: how much of the pixel at
// `pixel` (the target's) is inside all of them. Each takes six texels: kind and op (x: 0 a rounded
// rect; y: 1 for what is outside it), the rect and its radii in its own space, and the rows of the
// map from the target's pixels to that space.
float clipCoverage(float2 pixel, int count, float row) {
    float inside = 1;
    for (int i = 0; i < count; ++i) {
        float x = float(i * 6);
        float4 head = tableTexel(x, row), rect = tableTexel(x + 1, row), radii = tableTexel(x + 2, row);
        float3 r0 = tableTexel(x + 3, row).xyz, r1 = tableTexel(x + 4, row).xyz, r2 = tableTexel(x + 5, row).xyz;
        float3 h = float3(pixel, 1);
        float3 q = float3(dot(r0, h), dot(r1, h), dot(r2, h));
        float2 p = q.xy / q.z;
        // A pixel's steps in its space (the map's derivative there).
        float2 dx = float2(r0.x - r2.x * p.x, r1.x - r2.x * p.y) / q.z, dy = float2(r0.y - r2.y * p.x, r1.y - r2.y * p.y) / q.z;
        float c = q.z > 0 ? roundRectCoverage(p, rect, radii, dx, dy) : 0;
        inside *= head.y > 0.5 ? 1 - c : c;
    }
    return inside;
}

[shader("fragment")]
float4 fs(Varyings v) : SV_Target {
    int kind = int(v.kind + 0.5);
    int flags = int(v.flags + 0.5);
    // A device pixel's steps in the instance's space, exactly: the local point over w, and 1 over
    // w, go straight across the screen (their differences are exact), the local point doesn't
    // in perspective.
    float q = 1 / v.w;
    float2 lq = v.local * q;
    float2 dx = (ddx(lq) - v.local * ddx(q)) / q, dy = (ddy(lq) - v.local * ddy(q)) / q;
    float2 painted = v.local;
    if ((flags & 4) != 0) {
        float3 h = float3(v.local, 1);
        float3 q = float3(dot(v.radii.xyz, h), dot(v.uv.xyz, h), dot(v.shape.xyz, h));
        painted = q.xy / q.z;
    }
    float4 paint = paintAt(painted, v.color, v.paint, v.gradient);
    float4 src = paint;  // premultiplied, before the coverage
    float cover = 0;
    // Glyphs and images whose quad reaches past them (turned): sampled no further than their
    // texels, their edges anti-aliased.
    bool edged = (flags & 2) != 0;
    float2 lowUv = min(v.uv.xy, v.uv.zw) + v.params.zw, highUv = max(v.uv.xy, v.uv.zw) - v.params.zw;
    float2 texcoord = edged ? clamp(v.texcoord, lowUv, highUv) : v.texcoord;
    float edge = edged ? roundRectCoverage(v.local, v.shape, float4(0, 0, 0, 0), dx, dy) : 1;
    if (kind == FILL) {
        cover = roundRectCoverage(v.local, v.shape, v.radii, dx, dy);
    } else if (kind == STROKE) {
        float w = v.params.x;
        float4 inner = float4(v.shape.xy + w, v.shape.zw - w);
        float outside = roundRectCoverage(v.local, v.shape, v.radii, dx, dy);
        float inside = roundRectCoverage(v.local, inner, max(v.radii - w, 0), dx, dy);
        cover = saturate(outside - inside);
    } else if (kind == LINE) {
        float2 a = v.shape.xy, b = v.shape.zw;
        float len = length(b - a);
        float2 dir = len > 0 ? (b - a) / len : float2(1, 0), side = float2(-dir.y, dir.x);
        float2 d = v.local - a;
        float along = dot(d, dir) - len * 0.5, across = dot(d, side);
        // A rect turned along it: a band across, its two sides each cutting the pixel's box, and
        // one along (its square caps).
        float h = v.params.x, reach = len * 0.5 + h;
        float2 gSide = pixelSteps(side, dx, dy), gDir = pixelSteps(dir, dx, dy);
        float acrossIt = edgeCoverage(across - h, gSide) + edgeCoverage(-across - h, -gSide) - 1;
        float alongIt = edgeCoverage(along - reach, gDir) + edgeCoverage(-along - reach, -gDir) - 1;
        cover = saturate(acrossIt) * saturate(alongIt);
    } else if (kind == ARC) {
        // The ring between its two circles, each edge cutting the pixel's box (a ring thinner than
        // a pixel covers as much as it is wide); past its ends, the caps.
        float2 q = v.local - v.shape.xy;
        float len = length(q), radius = v.shape.z, h = v.params.x;
        float2 g = pixelSteps(len > 0 ? q / len : float2(1, 0), dx, dy);
        float ring = saturate(edgeCoverage(len - radius - h, g) + edgeCoverage(radius - h - len, -g) - 1);
        float4 cap = arcCap(q, radius, h, v.params.y, v.params.z, v.params.w > 0.5);
        cover = cap.w > 0.5 ? edgeCoverage(cap.x, pixelSteps(cap.yz, dx, dy)) : ring;
    } else if (kind == SHADOW) {
        float sigma = v.params.x;
        float blurred = sigma > 0.01 ? blurredRoundRect(v.local, v.shape, v.radii, sigma) : roundRectCoverage(v.local, v.shape, v.radii, dx, dy);
        // Inner: inside the rect (bounds, its radii in uv), where the blurred hole isn't.
        cover = v.params.y > 0.5 ? (1 - blurred) * roundRectCoverage(v.local, v.bounds, v.uv, dx, dy) : blurred;
    } else if (kind == GLYPH) {
        float4 texel = texture0.Sample(texcoord);
        bool colored = v.params.x > 0.5;  // an emoji: as it is, at the color's alpha
        src = colored ? float4(texel.rgb * texel.a, texel.a) * v.color.a : paint;
        cover = (colored ? 1 : texel.a) * edge;
    } else if (kind == IMAGE) {
        float4 texel = texture0.Sample(texcoord);
        src = float4(texel.rgb * texel.a, texel.a) * v.params.x;
        cover = edge;
    } else if (kind == LAYER) {
        float2 texel = clamp(v.local, v.params.xy + 0.5, v.params.zw - 0.5);
        src = texture0.Sample((texel - v.params.xy) * v.uv.zw) * v.color.a;
        // (Filtered where it is: a turned or scaled layer's pixels fall between the mask's texels.)
        cover = (flags & 1) != 0 ? pathCoverage(texture1.SampleLevel((v.local - v.radii.xy) * uniforms.texels.xy, 0).r, false)
                                 : roundRectCoverage(v.local, v.shape, v.radii, dx, dy);
    } else if (kind == PATH) {
        cover = pathCoverage(texture0.SampleLevel(maskTexcoord(v.local - v.params.xy), 0).r, v.params.z > 0.5);
    }
    cover *= clipCoverage(v.position.xy, int(v.filter.z + 0.5), v.filter.w);
    return finish(applyFilters(src, v.filter), cover, v.position.xy);
}
