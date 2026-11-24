// Placement of the game image in the window (ADR-006): the native image is shown at the original
// display aspect (4:3, 16:9 when the game's SCREEN RATIO option is on, or the actual window aspect
// during ultrawide flight), centred, with black bars when the aspects differ.
#pragma once

namespace ac04::render {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Largest rectangle with aspect aspect_w:aspect_h that fits in the window, centred, in whole pixels.
Rect FitAspect(int window_width, int window_height, int aspect_w, int aspect_h);

}  // namespace ac04::render
