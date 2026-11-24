// Render thread (ADR-002: presentation decoupled from the simulation; validation.md §2ai RT-1).
//
// The main thread runs the game's ticks (the game thread in lockstep), samples input and keeps the
// clock; the render thread turns the frame's GS data into a presented picture: GS processing,
// frame interpolation, Vulkan recording, submit and present. A frame's work for the render thread
// is one FrameJob, built by the main thread while the game is parked: it holds only copies (the
// steps' GIF streams, the display registers, the movie picture), never guest memory, so the game
// cannot see where or when it is drawn. At most one frame is in flight: Submit hands a job over only
// after the previous one has been presented, so v-sync still paces the main loop and the picture
// trails the game by at most one frame.
//
// Buffers travel back with the finished job (Submit returns it) and are put back into the game
// side's pools by the main thread while the game is parked.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "platform/render/gs/gs_source_mark.h"
#include "platform/render/vu1/vif1.h"
#include "platform/settings.h"

namespace ac04::render {

struct FrameJob {
    // GS data of the frame's steps in order. Fast-forward (--fast-until) adds segments whose draws
    // are off (GS registers and local memory only); the last segment is drawn.
    struct Segment {
        bool draws = true;
        std::vector<std::vector<std::uint8_t>> steps;
        std::vector<std::vector<gs::SourceMark>> marks;
    };
    std::vector<Segment> segments;
    // Program runs of the last segment's steps (frame interpolation; empty when it is off).
    std::vector<vu1::RunLog> runs;
    bool interpolating = false;
    bool interpolation_reset = false;  // interpolation was switched on or off before this frame
    std::uint32_t ticks_run = 0;
    double alpha = 0.0;  // TickClock::Alpha() after the frame's ticks

    std::uint64_t dispfb = 0, display = 0;  // sceGsPutDispEnv: DISPFB2, DISPLAY2
    int aspect_w = 4, aspect_h = 3;
    float hud_x_scale = 1.0f;  // flight-overlay compensation in ultrawide; 1 elsewhere
    struct Movie {
        std::uint64_t serial = 0;
        std::uint32_t guest_addr = 0, guest_bytes = 0;
        int width = 0, height = 0;
        std::vector<std::uint8_t> rgba;
    };
    std::optional<Movie> movie;  // a new decoded picture of the movie side path
    std::optional<platform::PresentationSettings> presentation;  // changed settings
    bool window_resized = false;
    int window_w = 0, window_h = 0;  // window size in pixels (SDL is queried on the main thread)

    // Native screenshot (development builds), taken before this frame is drawn, as before the render
    // thread: it shows the frame presented last.
    bool screenshot = false;
    std::uint64_t vblank = 0;

    // Back from the render thread.
    bool device_lost = false;  // DrawFrame failed: the main loop ends with an error message
    std::vector<std::vector<std::uint8_t>> returned_steps;  // emptied step buffers for the GS step pool
};

class RenderThread {
public:
    using Work = std::function<void(FrameJob&)>;

    ~RenderThread() { Stop(); }

    void Start(Work work);
    // Waits until the frame in flight is presented, hands `job` over and returns the finished one
    // (empty for the first call).
    FrameJob Submit(FrameJob job);
    // Waits until the frame in flight is presented (the debug overlay's ImGui frame, screenshots,
    // settings that touch the renderer directly).
    void WaitIdle();
    // Finishes the frame in flight, ends the thread and returns the finished job.
    FrameJob Stop();
    bool Running() const { return thread_.joinable(); }

private:
    void Loop();

    Work work_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    FrameJob job_;
    bool busy_ = false;  // job_ is being processed
    bool stop_ = false;
};

}  // namespace ac04::render
