#version 450
// GS primitives (src/platform/render/gs/gs_vulkan.cpp): positions in frame-buffer pixels with the
// depth value (GS Z / 2^bits, interpolated linearly in screen space as the GS does), GS colour bytes,
// texture coordinates (texels when FST = 1, else S, T, Q), fog coefficient F.

layout(location = 0) in vec3 in_pos;
layout(location = 1) in uvec4 in_color;
layout(location = 2) in vec3 in_tex;
layout(location = 3) in float in_fog;

layout(location = 0) out vec4 v_color;
layout(location = 1) noperspective out vec3 v_tex;
layout(location = 2) out float v_fog;

layout(push_constant) uniform Params {
    vec2 target_size;
    float overlay_center;
    float overlay_scale;
} pc;

void main() {
    // Ultrawide: only draws after a flight view's 3D/HUD boundary are narrowed about its centre.
    // The depth-copy pipeline puts an integer bit pattern here (a subnormal float); the lower bound
    // rejects it. Ordinary non-ultrawide draws use zero, also the identity.
    vec2 pos = in_pos.xy;
    if (pc.overlay_scale > 0.01 && pc.overlay_scale < 1.0)
        pos.x = pc.overlay_center + (pos.x - pc.overlay_center) * pc.overlay_scale;
    // GS pixel (x, y) is sampled at its integer coordinate; Vulkan samples pixel centres, so
    // positions are moved by half a pixel.
    vec2 p = (pos + 0.5) / pc.target_size * 2.0 - 1.0;
    gl_Position = vec4(p, in_pos.z, 1.0);
    v_color = vec4(in_color);
    v_tex = in_tex;
    v_fog = in_fog;
}
