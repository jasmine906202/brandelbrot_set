// core.cpp - the actual Mandelbrot maths, shared by every strategy.
#include "mandelbrot.hpp"

namespace mandel {

int iterate(double cr, double ci, int maxIter) {
    double zr = 0.0, zi = 0.0;     // z = zr + zi*i
    double zr2 = 0.0, zi2 = 0.0;   // zr*zr and zi*zi, kept to avoid recomputing
    int it = 0;

    // |z|^2 = zr2 + zi2. Once it exceeds 4 (|z| > 2) the point is guaranteed
    // to escape to infinity, so we can stop.
    while (zr2 + zi2 <= 4.0 && it < maxIter) {
        zi = 2.0 * zr * zi + ci;        // imaginary part of z^2 + c
        zr = zr2 - zi2 + cr;            // real part of z^2 + c
        zr2 = zr * zr;
        zi2 = zi * zi;
        ++it;
    }
    return it;
}

void computeRow(const Params& p, int y, int* out) {
    // Map the pixel row to a point on the imaginary axis ...
    const double ci = p.ymin + y * (p.ymax - p.ymin) / p.height;
    for (int x = 0; x < p.width; ++x) {
        // ... and each pixel column to a point on the real axis.
        const double cr = p.xmin + x * (p.xmax - p.xmin) / p.width;
        out[x] = iterate(cr, ci, p.maxIter);
    }
}

}  // namespace mandel