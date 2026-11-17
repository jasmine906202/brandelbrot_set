// mandelbrot.hpp - public interface of the Mandelbrot library.
//
// Every strategy (serial and parallel) has the same shape: it takes the
// problem description (Params) and returns a RunResult. That makes it easy
// to swap strategies in main.cpp and to compare their images and timings.
#pragma once

#include <vector>

namespace mandel {

// Problem description: image size, iteration limit and the region of the
// complex plane that is mapped onto the image.
struct Params {
    int width   = 1200;
    int height  = 800;
    int maxIter = 1000;
    double xmin = -2.5, xmax = 1.0;   // real axis
    double ymin = -1.0, ymax = 1.0;   // imaginary axis
};

// One iteration count per pixel, stored row by row (index = y * width + x).
using Image = std::vector<int>;

// Everything a single run produces.
struct RunResult {
    Image image;                       // the computed pixels
    double seconds = 0.0;              // wall-clock time of the whole run
    std::vector<double> threadBusy;    // busy time of every thread (seconds)
};

// ---------------------------------------------------------------- core ----
// Escape-time algorithm for one point c = cr + ci*i.
// Returns how many iterations z = z^2 + c needed to leave |z| <= 2,
// or maxIter if it never did (the point is considered inside the set).
int iterate(double cr, double ci, int maxIter);

// Computes one full image row y into out[0 .. width-1].
// All strategies call this same function, so their images are identical.
void computeRow(const Params& p, int y, int* out);

// -------------------------------------------------------------- serial ----
RunResult runSerial(const Params& p);

// ------------------------------------------------------------ parallel ----
// p = number of pthreads.

// V1: thread t gets one contiguous block of rows.
RunResult runStaticBlock(const Params& prm, int p);

// V2: thread t gets rows t, t+p, t+2p, ...
RunResult runInterleaved(const Params& prm, int p);

// V3a: threads grab "chunk" rows at a time from a shared std::atomic counter.
RunResult runDynamicAtomic(const Params& prm, int p, int chunk);

// V3b: same as V3a but the shared counter is protected by a pthread mutex.
RunResult runDynamicMutex(const Params& prm, int p, int chunk);

}  // namespace mandel