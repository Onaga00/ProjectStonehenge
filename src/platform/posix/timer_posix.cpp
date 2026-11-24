#include "platform/frame_pacer.h"

#include <thread>

namespace ac04::platform {

void InitTimerResolution() {
    // Nothing to do: Linux sleeps already have sub-millisecond granularity (hrtimers).
}

void PreciseSleepUntil(FramePacer::Clock::time_point deadline) {
    using namespace std::chrono;
    // Sleep for all but the last ~1 ms, then yield-spin (same split as the Windows timer).
    for (;;) {
        const auto remaining = deadline - FramePacer::Clock::now();
        if (remaining <= 0ns) return;
        if (remaining > 1500us) {
            std::this_thread::sleep_for(remaining - 1ms);
            continue;
        }
        std::this_thread::yield();
    }
}

}  // namespace ac04::platform
