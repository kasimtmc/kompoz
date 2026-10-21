// The path renderer's coverage: each edge of a path's polygons adds, to every pixel of the mask
// to its right, the part of the pixel it covers, signed by the way it goes (down: +1). Summed by
// additive blending into a 16-bit float mask, that is the winding number at the pixel's middle
// where nothing crosses it, and its exact area coverage where an edge does: shaders/primitives.slang
// turns it into coverage by the non-zero or even-odd rule. The same arithmetic as FreeType's
// rasterizer, done per pixel.
//
// Positions are pixels of the mask, y down.

#ifdef KOMPOZ_SPIRV
#define VERTEX_ID SV_VulkanVertexID
#else
#define VERTEX_ID SV_VertexID
#endif

struct Uniforms {
    float4 toClip;  // clip = position * toClip.xy + toClip.zw
};
[[vk::push_constant]] ConstantBuffer<Uniforms> uniforms;

struct Instance {
    float4 edge : EDGE;      // x0, y0, x1, y1
    float4 region : REGION;  // the path's part of the mask: x0, y0, x1, y1
};

struct Varyings {
    float4 position : SV_Position;
    nointerpolation float4 edge : EDGE;
};

[shader("vertex")]
Varyings vs(Instance instance, uint vertex : VERTEX_ID) {
    static const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    float2 corner = corners[vertex % 6];
    float4 e = instance.edge, r = instance.region;
    // From the pixel the edge starts in to the region's right side, the rows it spans.
    float4 quad = float4(floor(min(e.x, e.z)), floor(min(e.y, e.w)), r.z, ceil(max(e.y, e.w)));
    quad = float4(max(quad.xy, r.xy), min(quad.zw, r.zw));
    quad.zw = max(quad.zw, quad.xy);
    float2 p = lerp(quad.xy, quad.zw, corner);
    Varyings out;
    out.position = float4(p * uniforms.toClip.xy + uniforms.toClip.zw, 0, 1);
    out.edge = e;
    return out;
}

// The integral of clamp(1 - t, 0, 1) from minus infinity, up to t.
float rampIntegral(float t) {
    return t <= 0 ? t : (t < 1 ? t - 0.5 * t * t : 0.5);
}

[shader("fragment")]
float4 fs(Varyings v) : SV_Target {
    float2 cell = floor(v.position.xy);
    float4 e = v.edge;
    float sign = e.w > e.y ? 1 : -1;
    // The edge within the pixel's row.
    float top = max(min(e.y, e.w), cell.y), bottom = min(max(e.y, e.w), cell.y + 1);
    float height = bottom - top;
    if (height <= 0) return float4(0, 0, 0, 0);
    float dxdy = (e.z - e.x) / (e.w - e.y);
    // From the end nearer the row (the same whichever way the edge goes: one cut where it passes
    // behind the eye can be very far away).
    float2 from = abs(e.w - top) < abs(e.y - top) ? e.zw : e.xy;
    float xa = from.x + (top - from.y) * dxdy - cell.x, xb = from.x + (bottom - from.y) * dxdy - cell.x;
    // How much of the row's part of the pixel lies right of the edge: the average of
    // clamp(1 - x, 0, 1) along it.
    float covered = abs(xb - xa) < 1e-5 ? saturate(1 - xa) : (rampIntegral(xb) - rampIntegral(xa)) / (xb - xa);
    return float4(sign * height * covered, 0, 0, 0);
}
