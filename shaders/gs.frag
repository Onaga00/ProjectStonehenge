#version 450
// GS pixel pipeline subset (docs/platform/gs-subset.md): texture fetch with the GS wrap modes and
// nearest/bilinear filtering done by hand (texelFetch from the draw's texture image or a frame-buffer
// copy), the texture function (MODULATE/DECAL/HIGHLIGHT/HIGHLIGHT2) in 8-bit integer arithmetic,
// fog (FGE), the alpha test and the destination alpha test (DATE). Blending is fixed-function with dual-source blending: output 0 is the colour
// and the alpha written to the frame buffer, output 1 the blend factor (As/128 or FIX/128). Blend
// equations the fixed-function stage cannot express are done here from a copy of the target
// (flag 28) and written with blending off.

layout(location = 0) in vec4 v_color;
layout(location = 1) noperspective in vec3 v_tex;
layout(location = 2) in float v_fog;

// GS_SINGLE_SOURCE (gs_single_frag, the pipelines whose blend reads no SRC1 factor): output 0
// only. Intel GPUs before Gen12 run a dual-source pixel shader at half width (SIMD8).
#ifdef GS_SINGLE_SOURCE
layout(location = 0) out vec4 o_color;
#else
layout(location = 0, index = 0) out vec4 o_color;
layout(location = 0, index = 1) out vec4 o_factor;
#endif

layout(set = 1, binding = 0) uniform sampler2D tex;  // the draw's decoded texture (gs_state.h DecodedTexture)
layout(binding = 1) uniform sampler2D feedback;
layout(binding = 2) uniform sampler2D dst_copy;  // target region before a DATE draw (flag 22) or a shader blend (flag 28)
layout(binding = 3) uniform sampler2D movie;     // upscaled movie picture, bilinear (flag 27)
layout(binding = 4) uniform sampler2D clut_img;  // 16x16 palette drawn on the GPU (flag 29)

layout(push_constant) uniform Params {
    vec2 target_size;
    ivec4 rect;      // decoded texels: x, y unused (0), width, height of level 0; flag 27: see main
    ivec2 origin;    // texture coordinate of the rect's first texel
    ivec2 tex_size;  // 2^TW, 2^TH
    ivec4 region;    // MINU, MAXU, MINV, MAXV
    int flags;       // see gs_vulkan.cpp
    int aref;
    float fix;
    int fogcol;      // FOGCOL (flag 20)
    int tex1;        // MMIN 0-2, mip levels 3-5, LCM 6, L 7-8, K 9-20 (signed 1/16), MMAG 21
    int scale;       // internal scale of the target: host pixels per GS pixel
    int tex_scale;   // host texels per GS texel of the frame-buffer copy (flag 16)
    int alpha;       // ALPHA bits 0-7: A, B, C, D (flag 28)
    int text_alpha;  // full-coverage alpha of a glyph palette (flag 30)
} pc;

int Wrap(int c, int mode, int size, int mn, int mx) {
    if (mode == 0) return c & (size - 1);
    if (mode == 1) return clamp(c, 0, size - 1);
    if (mode == 2) return clamp(c, mn, mx);
    return (c & mn) | mx;
}

// Wrap of a host texel of a frame-buffer copy held at `s` host texels per GS texel: the wrap mode
// applies to the GS texel, the position inside it is kept.
int WrapScaled(int c, int mode, int size, int mn, int mx, int s) {
    if (s == 1) return Wrap(c, mode, size, mn, mx);
    if (mode == 1) return clamp(c, 0, size * s - 1);
    if (mode == 2) return clamp(c, mn * s, mx * s + s - 1);
    int n = c >= 0 ? c / s : -((s - 1 - c) / s);
    return Wrap(n, mode, size, mn, mx) * s + (c - n * s);
}

// Texel t of mipmap level `level`. Level 0 is the decoded rectangle (or the frame-buffer copy);
// levels 1.. are stored whole to the right of level 0, stacked top to bottom (gs_state.h
// DecodedTexture). HYPOTHESIS: the region clamp/repeat values scale with the level (>> level).
vec4 FetchTexel(ivec2 t, int level) {
    ivec2 size = max(pc.tex_size >> level, ivec2(1));
    if (level == 0 && (pc.flags & (1 << 16)) != 0) {
        // Frame-buffer copy, `tex_scale` host texels per GS texel.
        t.x = WrapScaled(t.x, (pc.flags >> 12) & 3, size.x, pc.region.x, pc.region.y, pc.tex_scale);
        t.y = WrapScaled(t.y, (pc.flags >> 14) & 3, size.y, pc.region.z, pc.region.w, pc.tex_scale);
        return floor(texelFetch(feedback, t, 0) * 255.0 + 0.5);
    }
    t.x = Wrap(t.x, (pc.flags >> 12) & 3, size.x, pc.region.x >> level, pc.region.y >> level);
    t.y = Wrap(t.y, (pc.flags >> 14) & 3, size.y, pc.region.z >> level, pc.region.w >> level);
    if (level == 0) {
        ivec2 a = clamp(t - pc.origin, ivec2(0), pc.rect.zw - 1);
        return floor(texelFetch(tex, a, 0) * 255.0 + 0.5);
    }
    int y = 0;
    for (int k = 1; k < level; ++k) y += max(pc.tex_size.y >> k, 1);
    ivec2 a = clamp(t, ivec2(0), size - 1);
    return floor(texelFetch(tex, ivec2(pc.rect.z, y) + a, 0) * 255.0 + 0.5);
}

// Flag 29: the texture holds 8-bit palette indices (in red) of a palette the GPU drew; each texel
// is looked up in its 16x16 CT32 image (CSM1: bits 3 and 4 of the index swapped), before filtering.
vec4 Fetch(ivec2 t, int level) {
    vec4 c = FetchTexel(t, level);
    if ((pc.flags & (1 << 29)) == 0) return c;
    int i = int(c.r);
    int p = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
    return floor(texelFetch(clut_img, ivec2(p & 15, p >> 4), 0) * 255.0 + 0.5);
}

// Level `level` sampled at level-0 texel coordinates uv, nearest or bilinear. A scaled
// frame-buffer copy is sampled on its own (host) texel grid.
vec4 Sample(vec2 uv, int level, bool linear) {
    vec2 u = uv / float(1 << level);
    if (level == 0 && (pc.flags & (1 << 16)) != 0) u *= float(pc.tex_scale);
    if (linear) {
        vec2 b = u - 0.5;
        ivec2 i = ivec2(floor(b));
        vec2 f = floor(fract(b) * 16.0) / 16.0;  // GS: 4-bit weights
        vec4 t00 = Fetch(i, level), t10 = Fetch(i + ivec2(1, 0), level);
        vec4 t01 = Fetch(i + ivec2(0, 1), level), t11 = Fetch(i + ivec2(1, 1), level);
        return floor(mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y));
    }
    return Fetch(ivec2(floor(u)), level);
}

void main() {
    // Flag 26: a 2D sprite drawn at internal scale > 1 samples at the UV of the native pixel the
    // host pixel lies in (native pixel n covers GS positions [n - 1/2, n + 1/2), sample point n =
    // floor(g + 1/2), g = the host pixel's GS position), so its texels map 1:1 to native pixels as
    // on the PS2. Derivatives first (uniform control flow).
    vec2 tex_xy = v_tex.xy;
    vec2 uv_dx = dFdx(v_tex.xy), uv_dy = dFdy(v_tex.xy);  // texels per host pixel (flag 30, FST sprites)
    if ((pc.flags & (1 << 26)) != 0) {
        vec2 g = gl_FragCoord.xy / float(pc.scale) - 0.5;
        vec2 dg = (floor(g + 0.5) - g) * float(pc.scale);
        tex_xy += dFdx(v_tex.xy) * dg.x + dFdy(v_tex.xy) * dg.y;
    }
    // Flag 22: destination alpha test. The pixel is drawn only when bit 7 of the stored alpha equals
    // DATM (flag 23).
    if ((pc.flags & (1 << 22)) != 0) {
        float da = floor(texelFetch(dst_copy, ivec2(gl_FragCoord.xy), 0).a * 255.0 + 0.5);
        if ((da >= 128.0) != ((pc.flags & (1 << 23)) != 0)) discard;
    }
    vec4 c = floor(v_color + 0.5);
    if ((pc.flags & 1) != 0) {
        bool fst = (pc.flags & 2) != 0;
        vec2 uv = fst ? tex_xy : tex_xy / v_tex.z * vec2(pc.tex_size);
        // The GS addresses texels in fixed point with 4 fractional bits. Rounding to that grid
        // removes float interpolation error: a 1:1 sprite gives exact integers at pixel positions,
        // and 0.9999 would pick the texel on the left (seams every 16 pixels in movies).
        // Flag 30 (sharp text, port-features.md §2) samples at the host pixel's own position.
        bool sharp_text = (pc.flags & (1 << 30)) != 0;
        if (!sharp_text) uv = floor(uv * 16.0 + 0.5) / 16.0;
        // Filter choice (TEX1): LOD = K with LCM = 1, else log2(1/|Q|) * 2^L + K. LOD <= 0 magnifies
        // (MMAG); above, MMIN: 0 nearest, 1 linear, 2/4 nearest/linear in the nearest mip level
        // (LOD rounded), 3/5 nearest/linear in the two nearest levels, blended (trilinear). LOD is
        // clamped to the decoded levels (MXL).
        int mmin = pc.tex1 & 7;
        int levels = (pc.tex1 >> 3) & 7;
        float k = float((pc.tex1 << 11) >> 20) / 16.0;
        float q = fst ? 1.0 : abs(v_tex.z);
        float lod = ((pc.tex1 >> 6) & 1) != 0 ? k : log2(1.0 / max(q, 1e-30)) * float(1 << ((pc.tex1 >> 7) & 3)) + k;
        bool min_linear = mmin == 1 || mmin == 4 || mmin == 5;
        vec4 t;
        if (sharp_text) {
            // Flag 30: a glyph sprite at internal scale > 1 (gs_vulkan.cpp CoverageMaskAlpha). The
            // glyph's coverage is in the palette's alpha alone, so: bilinear at full precision
            // (alpha-weighted colour, no dark fringe), then the alpha edge is drawn as a step at
            // half coverage, anti-aliased over about one host pixel (fwidth), the way distance-
            // field fonts are drawn. Presentation only; flag 26 is not set for these draws.
            vec2 b = uv - 0.5;
            ivec2 i = ivec2(floor(b));
            vec2 f = fract(b);
            vec4 t00 = Fetch(i, 0), t10 = Fetch(i + ivec2(1, 0), 0);
            vec4 t01 = Fetch(i + ivec2(0, 1), 0), t11 = Fetch(i + ivec2(1, 1), 0);
            float a = mix(mix(t00.a, t10.a, f.x), mix(t01.a, t11.a, f.x), f.y);
            vec3 pm = mix(mix(t00.rgb * t00.a, t10.rgb * t10.a, f.x), mix(t01.rgb * t01.a, t11.rgb * t11.a, f.x), f.y);
            vec3 rgb = a > 0.0 ? pm / a : t00.rgb;
            // Change of alpha per host pixel, from the bilinear slope and the texels per host pixel
            // (derivatives taken before any discard): fwidth(a) without derivatives of a.
            vec2 da = vec2(mix(t10.a - t00.a, t11.a - t01.a, f.y), mix(t01.a - t00.a, t11.a - t10.a, f.x));
            float w = abs(dot(da, uv_dx)) + abs(dot(da, uv_dy));
            float full = float(pc.text_alpha);
            float edge = clamp((a - 0.5 * full) / max(w, 1e-3) + 0.5, 0.0, 1.0);
            // Only the edge is sharpened: inside the stroke the font's own (bilinear) alpha is kept,
            // so the text keeps the game's brightness and shadow weight.
            t = floor(vec4(rgb, edge * max(a, 0.5 * full)) + 0.5);
        } else if ((pc.flags & (1 << 27)) != 0) {
            // Movie column (ADR-006 §5): rect.xy = picture pixel of texel (0, 0), rect.zw = picture
            // size; the upscaled picture is sampled bilinearly at that position.
            t = floor(texture(movie, (vec2(pc.rect.xy) + uv) / vec2(pc.rect.zw)) * 255.0 + 0.5);
        } else if (lod <= 0.0) {
            t = Sample(uv, 0, ((pc.tex1 >> 21) & 1) != 0);
        } else if (mmin < 2 || levels == 0) {
            t = Sample(uv, 0, min_linear);
        } else {
            float l = min(lod, float(levels));
            if (mmin == 2 || mmin == 4) {
                t = Sample(uv, min(int(floor(l + 0.5)), levels), min_linear);
            } else {
                int li = int(floor(l));
                if (li >= levels) {
                    t = Sample(uv, levels, min_linear);
                } else {
                    t = floor(mix(Sample(uv, li, min_linear), Sample(uv, li + 1, min_linear), l - float(li)));
                }
            }
        }
        int tfx = (pc.flags >> 2) & 3;
        bool tcc = (pc.flags & (1 << 4)) != 0;
        vec3 rgb;
        float a;
        if (tfx == 1) {  // DECAL
            rgb = t.rgb;
            a = tcc ? t.a : c.a;
        } else {
            rgb = floor(t.rgb * c.rgb / 128.0);
            if (tfx >= 2) rgb += c.a;  // HIGHLIGHT, HIGHLIGHT2
            if (tfx == 0) a = tcc ? floor(t.a * c.a / 128.0) : c.a;
            else if (tfx == 2) a = tcc ? t.a + c.a : c.a;
            else a = tcc ? t.a : c.a;
        }
        c = vec4(min(rgb, 255.0), min(a, 255.0));
    }
    // Flag 20: fog (PRIM.FGE). The colour moves toward FOGCOL as F falls (F = 255: no fog); alpha is
    // not fogged. F is interpolated across the primitive.
    if ((pc.flags & (1 << 20)) != 0) {
        vec3 fc = vec3(pc.fogcol & 0xFF, (pc.fogcol >> 8) & 0xFF, (pc.fogcol >> 16) & 0xFF);
        c.rgb = floor(mix(fc, c.rgb, clamp(v_fog, 0.0, 255.0) / 255.0));
    }
    if ((pc.flags & (1 << 6)) != 0) {
        int atst = (pc.flags >> 7) & 7;
        int av = int(c.a);
        bool pass = atst == 1 || (atst == 2 && av < pc.aref) || (atst == 3 && av <= pc.aref) ||
                    (atst == 4 && av == pc.aref) || (atst == 5 && av >= pc.aref) ||
                    (atst == 6 && av > pc.aref) || (atst == 7 && av != pc.aref);
        // Flag 19: second draw of TEST.AFAIL RGB_ONLY, which keeps only the failing pixels.
        if (pass == ((pc.flags & (1 << 19)) != 0)) discard;
    }
    // Flag 25: PRIM.AA1 without ABE blends with As = coverage, 1.0 inside the primitive.
    float factor = ((pc.flags >> 17) & 1) != 0 ? pc.fix / 128.0 : (((pc.flags >> 25) & 1) != 0 ? 1.0 : c.a / 128.0);
    // Flag 28: the GS blend (A - B) * C >> 7 + D with A, B, D in {Cs, Cd, 0} and C in {As, Ad, FIX},
    // clamped to 0-255 (COLCLAMP 1), on the target copied before the draw. Alpha is not blended.
    if ((pc.flags & (1 << 28)) != 0) {
        vec4 dst = floor(texelFetch(dst_copy, ivec2(gl_FragCoord.xy), 0) * 255.0 + 0.5);
        int sa = pc.alpha & 3, sb = (pc.alpha >> 2) & 3, sc = (pc.alpha >> 4) & 3, sd = (pc.alpha >> 6) & 3;
        vec3 ca = sa == 0 ? c.rgb : (sa == 1 ? dst.rgb : vec3(0.0));
        vec3 cb = sb == 0 ? c.rgb : (sb == 1 ? dst.rgb : vec3(0.0));
        vec3 cd = sd == 0 ? c.rgb : (sd == 1 ? dst.rgb : vec3(0.0));
        float as = ((pc.flags >> 25) & 1) != 0 ? 128.0 : c.a;
        float k = sc == 0 ? as : (sc == 1 ? dst.a : pc.fix);
        c.rgb = clamp(floor((ca - cb) * k / 128.0) + cd, 0.0, 255.0);
    }
    // Flag 24: 16-bit frame buffer (CT16/CT16S). The GS stores the colour's top 5 bits and alpha
    // bit 7; the RGBA8 target keeps them as multiples of 8 and 0x80/0 (gs_conv.comp reads them back).
    if ((pc.flags & (1 << 24)) != 0) c = vec4(floor(c.rgb / 8.0) * 8.0, c.a >= 128.0 ? 128.0 : 0.0);
    // Flag 18: C = Ad blending (gs_vulkan.cpp MapBlend) needs Cs / 128 against DST_ALPHA = Ad / 255.
    o_color = vec4(c.rgb / (((pc.flags & (1 << 18)) != 0) ? 128.0 : 255.0), c.a / 255.0);
#ifndef GS_SINGLE_SOURCE
    o_factor = vec4(factor);
#endif
}
