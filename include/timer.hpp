// timer.hpp - tiny wall-clock helper used by every strategy.
#pragma once

#include <chrono>

namespace mandel {

// Seconds since an arbitrary fixed point. steady_clock never jumps backwards
// (unlike system_clock), so differences between two calls are safe to use as
// elapsed time.
inline double now() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

}  // namespace mandel