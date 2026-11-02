// Minimal debug overlay (milestone D, docs/port/port-features.md §8). Part of the optional debug layer:
// created when settings.json "debug.overlay_enabled" is true (default on in development builds) or
// the frame-rate overlay is on ("debug.fps_overlay", --fps-overlay, or its checkbox in the
// Performance panel: frame rate and frame time in small text at the top right of the window),
// opened and closed with the toggle character ("\" by default, or "key:<SDL scancode name>").
//
// Panels ("Sim: no"): performance and frame timing, scene inspector with the
// RequestScene/RequestSubState log, memory viewer at PS2 addresses with docs/symbols.csv names,
// live session log, input viewer (host pads, pressures, the game's pad type), all read-only; and
// Graphics, which changes the presentation settings that apply while the port runs (presentation
// only, never game state; the host applies the request after the frame is built).
//
// Drawn by the output pass after the 4:3 game image, at host resolution, into the swapchain image
// (Renderer overlay hook). Guest memory is read only while the game thread is parked (ADR-002).
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/types.h"
#include "platform/input.h"
#include "platform/settings.h"

union SDL_Event;
struct SDL_Window;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
struct ImVec2;

namespace ac04::render {
class Renderer;
}

namespace ac04::platform {

// Values the host loop hands to the overlay each presented frame.
struct OverlayFrameData {
    double frame_seconds = 0.0;   // host time since the previous presented frame
    u32 ticks_run = 0;            // simulation steps run before this frame
    u64 vblank_count = 0;         // simulation steps since boot
    u64 dropped_ticks = 0;        // ticks dropped by the catch-up limit (all time)
    double alpha = 0.0;           // interpolation fraction
    bool game_running = false;
    // DMA intake (milestone C: counted, not drawn).
    u64 gif_qwords = 0, vif1_qwords = 0;  // in the last simulation step
    u64 gif_sends = 0, vif1_sends = 0;    // all time
    u64 bad_chains = 0;
    std::array<u64, 5> dispenv{};          // PMODE, SMODE2, DISPFB, DISPLAY, BGCOLOR
    PresentationSettings presentation;     // in force this session (the Graphics panel edits a copy)
    std::array<PadState, 2> pads{};        // live host input per port (input backend)
    std::array<PadDeviceInfo, 2> pad_devices{};
};

// A change from the Graphics panel: the new presentation values, applied at once by the host;
// `save` also writes them to settings.json.
struct PresentationRequest {
    PresentationSettings values;
    bool save = false;
};

class DebugOverlay {
public:
    DebugOverlay();
    ~DebugOverlay();

    // Creates the ImGui context and backends when `settings.overlay_enabled` or
    // `settings.fps_overlay`; otherwise does nothing and every other call is a no-op. Can be called
    // again after a settings change.
    bool Apply(const DebugSettings& settings, SDL_Window* window, render::Renderer& renderer, std::string& error);
    void Shutdown();

    // Returns true when the event was consumed by the overlay (the toggle, or keyboard/mouse input
    // while the overlay has focus).
    bool HandleEvent(const SDL_Event& event);
    // The keyboard belongs to the overlay (not forwarded to the game's pad mapping).
    bool WantsKeyboard() const;

    // Builds the UI for this frame; call before Renderer::DrawFrame.
    void BuildFrame(const OverlayFrameData& data);
    // The Graphics panel's change in the frame just built, if any (taken once).
    std::optional<PresentationRequest> TakePresentationRequest();

    bool Active() const { return active_; }
    bool Visible() const { return active_ && visible_; }
    bool FpsVisible() const { return active_ && fps_; }

private:
    void Draw(VkCommandBuffer cmd);
    void EndFrame();
    ImVec2 Scaled(float x, float y) const;
    void DrawPerformance(const OverlayFrameData& data);
    void DrawFps();
    void DrawScene();
    void DrawMemory();
    void DrawLog();
    void DrawGraphics(const OverlayFrameData& data);
    void DrawInput(const OverlayFrameData& data);
    void LoadSymbols();

    struct Symbol {
        u32 address;
        std::string name;
    };

    bool active_ = false;
    bool panels_ = false;   // debug.overlay_enabled: the panels can be opened with the toggle
    bool fps_ = false;      // frame-rate overlay shown
    bool visible_ = false;
    bool frame_built_ = false;
    std::string toggle_text_ = "\\";
    int toggle_scancode_ = 0;   // non-zero when the toggle is "key:<name>"
    render::Renderer* renderer_ = nullptr;
    SDL_Window* window_ = nullptr;
    float scale_ = 1.0f;            // UI scale from the window height (UiScale)
    bool unfocus_pending_ = false;  // clear panel focus on the next frame (after opening)
    void* last_render_pass_ = nullptr;

    bool show_perf_ = true, show_scene_ = true, show_memory_ = false, show_log_ = false;
    bool show_graphics_ = false, show_input_ = false;
    std::optional<PresentationRequest> presentation_request_;
    std::array<float, 240> frame_ms_{};
    size_t frame_ms_pos_ = 0;

    std::vector<Symbol> symbols_;   // sorted by address
    std::string symbols_source_;
    u32 mem_address_ = 0x0029F380;  // g_MainContext
    u32 mem_target_ = 0x0029F380;   // address last looked up (its row is highlighted)
    int mem_view_ = 0;              // 0 bytes, 1 u32, 2 f32
    char goto_text_[64] = {};
    std::string goto_error_;
    char log_filter_[64] = {};
};

}  // namespace ac04::platform
