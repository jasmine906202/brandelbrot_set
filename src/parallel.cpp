// parallel.cpp - the pthread strategies.
//
// The structure is:
//   runThreads()  creates p pthreads, runs a "body" in each, joins them and
//                 collects timings. It is the only place that touches pthread
//                 create/join.
//   run*()        each strategy only decides WHICH ROWS a thread computes.
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

#include "mandelbrot.hpp"
#include "timer.hpp"

namespace mandel {
namespace {

// Per-thread bookkeeping. alignas(64) gives each Worker its own cache line, so
// one thread writing its "busy" time never invalidates a neighbour's cache
// line (false sharing).
struct alignas(64) Worker {
    const std::function<void(int)>* job = nullptr;
    int id = 0;
    double busy = 0.0;
    pthread_t handle{};
};

// Entry point every pthread starts in (pthread needs a plain C function).
void* threadMain(void* arg) {
    Worker* w = static_cast<Worker*>(arg);
    const double t0 = now();
    (*w->job)(w->id);
    w->busy = now() - t0;  // how long this thread actually worked
    return nullptr;
}

using Body = std::function<void(int /*threadId*/, Image& /*image*/)>;

RunResult runThreads(const Params& prm, int p, const Body& body) {
    if (p < 1) throw std::invalid_argument("thread count must be >= 1");

    RunResult r;
    r.image.assign(static_cast<std::size_t>(prm.width) * prm.height, 0);

    std::vector<Worker> workers(p);
    const std::function<void(int)> job = [&](int id) { body(id, r.image); };

    const double t0 = now();  // timing includes thread creation and joining

    int created = 0;
    bool failed = false;
    for (int i = 0; i < p; ++i) {
        workers[i].job = &job;
        workers[i].id = i;
        if (pthread_create(&workers[i].handle, nullptr, threadMain, &workers[i]) != 0) {
            failed = true;
            break;
        }
        ++created;
    }
    // Always join what we started, even if a later create failed.
    for (int i = 0; i < created; ++i) pthread_join(workers[i].handle, nullptr);

    r.seconds = now() - t0;
    if (failed) throw std::runtime_error("pthread_create failed");

    for (const Worker& w : workers) r.threadBusy.push_back(w.busy);
    return r;
}

// RAII wrapper so the mutex is always destroyed.
struct PthreadMutex {
    pthread_mutex_t m;
    PthreadMutex() { pthread_mutex_init(&m, nullptr); }
    ~PthreadMutex() { pthread_mutex_destroy(&m); }
    PthreadMutex(const PthreadMutex&) = delete;
    PthreadMutex& operator=(const PthreadMutex&) = delete;
};

int checkedChunk(int chunk, int height) {
    if (chunk < 1) throw std::invalid_argument("chunk must be >= 1");
    return std::min(chunk, std::max(1, height));
}

}  // namespace

// ------------------------------------------------------------------ V1 ----
// Thread t computes rows [t*rowsPer, (t+1)*rowsPer). No synchronisation at
// all, but rows near the middle of the image are far more expensive, so the
// busiest thread decides the total run time.
RunResult runStaticBlock(const Params& prm, int p) {
    const int rowsPer = (prm.height + p - 1) / p;  // ceil(height / p)
    return runThreads(prm, p, [&](int id, Image& img) {
        const int begin = std::min(prm.height, id * rowsPer);
        const int end = std::min(prm.height, begin + rowsPer);
        for (int y = begin; y < end; ++y)
            computeRow(prm, y, &img[static_cast<std::size_t>(y) * prm.width]);
    });
}

// ------------------------------------------------------------------ V2 ----
// Thread t computes rows t, t+p, t+2p, ... Still no synchronisation, but
// expensive and cheap rows are spread evenly over all threads.
RunResult runInterleaved(const Params& prm, int p) {
    return runThreads(prm, p, [&](int id, Image& img) {
        for (int y = id; y < prm.height; y += p)
            computeRow(prm, y, &img[static_cast<std::size_t>(y) * prm.width]);
    });
}

// ----------------------------------------------------------------- V3a ----
// Threads repeatedly take the next "chunk" rows from one shared atomic
// counter, so a thread that finishes early simply takes more work.
RunResult runDynamicAtomic(const Params& prm, int p, int chunk) {
    chunk = checkedChunk(chunk, prm.height);
    std::atomic<int> next{0};
    return runThreads(prm, p, [&](int, Image& img) {
        for (;;) {
            const int begin = next.fetch_add(chunk);
            if (begin >= prm.height) break;
            const int end = std::min(prm.height, begin + chunk);
            for (int y = begin; y < end; ++y)
                computeRow(prm, y, &img[static_cast<std::size_t>(y) * prm.width]);
        }
    });
}

// ----------------------------------------------------------------- V3b ----
// Same as V3a, but the shared counter is protected by a pthread mutex.
RunResult runDynamicMutex(const Params& prm, int p, int chunk) {
    chunk = checkedChunk(chunk, prm.height);
    PthreadMutex lock;
    int next = 0;
    return runThreads(prm, p, [&](int, Image& img) {
        for (;;) {
            pthread_mutex_lock(&lock.m);
            const int begin = next;
            next += chunk;
            pthread_mutex_unlock(&lock.m);
            if (begin >= prm.height) break;
            const int end = std::min(prm.height, begin + chunk);
            for (int y = begin; y < end; ++y)
                computeRow(prm, y, &img[static_cast<std::size_t>(y) * prm.width]);
        }
    });
}

}  // namespace mandel
