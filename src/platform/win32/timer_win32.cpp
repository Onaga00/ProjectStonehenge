#include "platform/frame_pacer.h"

#include <windows.h>
#include <timeapi.h>

#include <thread>

namespace ac04::platform {

namespace {

// Restores the scheduler granularity at exit (pairs with timeBeginPeriod).
struct TimerPeriod {
    TimerPeriod() { timeBeginPeriod(1); }
    ~TimerPeriod() { timeEndPeriod(1); }
};

// One high-resolution waitable timer per thread, closed when the thread ends.
struct ThreadTimer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ~ThreadTimer() {
        if (handle) CloseHandle(handle);
    }
};

}  // namespace

void InitTimerResolution() {
    // 1 ms scheduler granularity for the coarse part of the wait.
    static TimerPeriod period;
}

void PreciseSleepUntil(FramePacer::Clock::time_point deadline) {
    using namespace std::chrono;
    // High-resolution waitable timer for all but the last ~1 ms, then yield-spin.
    static thread_local ThreadTimer thread_timer;
    const HANDLE timer = thread_timer.handle;
    for (;;) {
        const auto remaining = deadline - FramePacer::Clock::now();
        if (remaining <= 0ns) return;
        if (remaining > 1500us && timer) {
            LARGE_INTEGER due;
            // Relative time in 100 ns units (negative = relative).
            due.QuadPart = -static_cast<LONGLONG>(duration_cast<nanoseconds>(remaining - 1ms).count() / 100);
            if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0)) {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        std::this_thread::yield();
    }
}

}  // namespace ac04::platform
