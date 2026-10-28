#include "common/port_hooks.h"

#include <numeric>

namespace ac04::port {

MenuOptions& Menu() {
    static MenuOptions options;
    return options;
}

RenderMarks& Render() {
    static RenderMarks marks;
    return marks;
}

BootOptions& Boot() {
    static BootOptions options;
    return options;
}

InputHooks& Input() {
    static InputHooks hooks;
    return hooks;
}

DisplayOptions& Display() {
    static DisplayOptions options;
    return options;
}

void SetDisplayOptions(bool wide, bool ultrawide, int window_width, int window_height, bool keep_4x3_fov) {
    DisplayOptions& o = Display();
    o.aspect_w = wide ? 16 : 4;
    o.aspect_h = wide ? 9 : 3;
    o.projection_x_scale = 1.0f;
    o.hud_x_scale = 1.0f;
    o.ultrawide = false;
    o.keep_4x3_fov = keep_4x3_fov;
    o.window_aspect = 0.0f;
    if (!wide || !ultrawide || window_width <= 0 || window_height <= 0) return;
    // A small tolerance avoids treating rounding around exactly 16:9 as an ultrawide display.
    if (900LL * window_width <= 1616LL * window_height) return;
    const float aspect = static_cast<float>(window_width) / static_cast<float>(window_height);
    o.ultrawide = true;
    o.window_aspect = aspect;
    o.projection_x_scale = aspect / (16.0f / 9.0f);
    o.hud_x_scale = 1.0f / o.projection_x_scale;
    const int divisor = std::gcd(window_width, window_height);
    o.aspect_w = window_width / divisor;
    o.aspect_h = window_height / divisor;
}

float UltrawideProjectionWidth(float width_4x3, float width_16x9) {
    const DisplayOptions& o = Display();
    if (!o.ultrawide) return width_16x9;
    if (o.keep_4x3_fov) return width_4x3 * (o.window_aspect / (4.0f / 3.0f));
    return width_16x9 * o.projection_x_scale;
}

bool UltrawideUses4x3Distances() {
    const DisplayOptions& o = Display();
    return o.ultrawide && o.keep_4x3_fov;
}

}  // namespace ac04::port
