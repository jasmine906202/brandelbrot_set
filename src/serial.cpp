// serial.cpp - single-threaded baseline. All speedups are measured against it.
#include <cstddef>

#include "mandelbrot.hpp"
#include "timer.hpp"

namespace mandel {

RunResult runSerial(const Params& p) {
    RunResult r;
    r.image.assign(static_cast<std::size_t>(p.width) * p.height, 0);

    const double t0 = now();
    for (int y = 0; y < p.height; ++y) {
        computeRow(p, y, &r.image[static_cast<std::size_t>(y) * p.width]);
    }
    r.seconds = now() - t0;

    r.threadBusy.push_back(r.seconds);  // one "thread", busy the whole time
    return r;
}

}  // namespace mandel