// main.cpp - command-line driver: runs the serial baseline and the selected
// parallel strategies, checks their output, prints a table and (optionally)
// writes a CSV file and a PPM image.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>  // only for hardware_concurrency(); all parallel work uses pthreads
#include <vector>

#include "mandelbrot.hpp"

using namespace mandel;

namespace {

const char* const kAllStrategies[] = {"static", "interleaved", "dynamic-atomic", "dynamic-mutex"};

struct Options {
    Params params;
    std::vector<int> threads;
    std::vector<std::string> strategies;
    int chunk = 4;
    int runs = 5;
    std::string imagePath;
    std::string csvPath;
    bool verify = true;
};

// What we keep from one run once its image has been checked.
struct Sample {
    double seconds;
    std::vector<double> busy;
};

void usage(const char* prog) {
    std::printf(
        "Usage: %s [options]\n\n"
        "  --width N          image width                      (default 1200)\n"
        "  --height N         image height                     (default 800)\n"
        "  --iter N           maximum iterations per pixel     (default 1000)\n"
        "  --threads a,b,c    thread counts to test            (default 1,2,4.. up to #cores)\n"
        "  --strategy list    static,interleaved,dynamic-atomic,dynamic-mutex or all\n"
        "  --chunk N          rows per grab for dynamic-*      (default 4)\n"
        "  --runs N           repetitions; the median is used  (default 5)\n"
        "  --image file.ppm   write the serial image to a PPM file\n"
        "  --csv file.csv     append results to a CSV file\n"
        "  --no-verify        skip the serial-vs-parallel image comparison\n"
        "  -h, --help         show this help\n",
        prog);
}

int toInt(const std::string& s, const std::string& option) {
    try {
        std::size_t pos = 0;
        const int v = std::stoi(s, &pos);
        if (pos != s.size()) throw std::invalid_argument("trailing characters");
        return v;
    } catch (const std::exception&) {
        throw std::invalid_argument("option " + option + " expects an integer, got '" + s + "'");
    }
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

Options parseArgs(int argc, char** argv) {
    Options o;
    auto value = [&](int& i) -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value for ") + argv[i]);
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--width") o.params.width = toInt(value(i), a);
        else if (a == "--height") o.params.height = toInt(value(i), a);
        else if (a == "--iter") o.params.maxIter = toInt(value(i), a);
        else if (a == "--threads") {
            for (const std::string& t : split(value(i))) o.threads.push_back(toInt(t, a));
        } else if (a == "--strategy") {
            for (const std::string& s : split(value(i))) {
                if (s == "all") {
                    for (const char* k : kAllStrategies) o.strategies.push_back(k);
                } else {
                    if (std::find(std::begin(kAllStrategies), std::end(kAllStrategies), s) ==
                        std::end(kAllStrategies))
                        throw std::invalid_argument("unknown strategy '" + s + "'");
                    o.strategies.push_back(s);
                }
            }
        }
        else if (a == "--chunk") o.chunk = toInt(value(i), a);
        else if (a == "--runs") o.runs = toInt(value(i), a);
        else if (a == "--image") o.imagePath = value(i);
        else if (a == "--csv") o.csvPath = value(i);
        else if (a == "--no-verify") o.verify = false;
        else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            std::exit(0);
        } else throw std::invalid_argument("unknown option '" + a + "' (try --help)");
    }

    if (o.params.width < 1 || o.params.height < 1) throw std::invalid_argument("width and height must be >= 1");
    if (o.params.maxIter < 1) throw std::invalid_argument("--iter must be >= 1");
    if (o.chunk < 1) throw std::invalid_argument("--chunk must be >= 1");
    if (o.runs < 1) throw std::invalid_argument("--runs must be >= 1");

    if (o.strategies.empty())
        for (const char* k : kAllStrategies) o.strategies.push_back(k);

    if (o.threads.empty()) {  // default: 1, 2, 4, ... and finally the core count
        const int cores = std::max(1u, std::thread::hardware_concurrency());
        for (int t = 1; t < cores; t *= 2) o.threads.push_back(t);
        o.threads.push_back(cores);
    }
    for (int t : o.threads)
        if (t < 1) throw std::invalid_argument("thread counts must be >= 1");
    return o;
}

RunResult runStrategy(const std::string& name, const Params& prm, int p, int chunk) {
    if (name == "static") return runStaticBlock(prm, p);
    if (name == "interleaved") return runInterleaved(prm, p);
    if (name == "dynamic-atomic") return runDynamicAtomic(prm, p, chunk);
    return runDynamicMutex(prm, p, chunk);
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// max(busy) / mean(busy); 1.0 means every thread worked equally long.
double imbalance(const std::vector<double>& busy) {
    if (busy.empty()) return 1.0;
    const double mean = std::accumulate(busy.begin(), busy.end(), 0.0) / busy.size();
    return mean > 0.0 ? *std::max_element(busy.begin(), busy.end()) / mean : 1.0;
}

void writePpm(const std::string& path, const Params& p, const Image& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "P6\n" << p.width << " " << p.height << "\n255\n";
    std::vector<unsigned char> rgb(img.size() * 3);
    for (std::size_t i = 0; i < img.size(); ++i) {
        unsigned char r = 0, g = 0, b = 0;  // points inside the set are black
        if (img[i] < p.maxIter) {
            const double t = std::pow(static_cast<double>(img[i]) / p.maxIter, 0.25);  // gamma: brighten the shallow bands
            r = static_cast<unsigned char>(255 * std::min(1.0, 9.0 * (1 - t) * t * t * t));
            g = static_cast<unsigned char>(255 * std::min(1.0, 15.0 * (1 - t) * (1 - t) * t * t));
            b = static_cast<unsigned char>(255 * std::min(1.0, 8.5 * (1 - t) * (1 - t) * (1 - t) * t));
        }
        rgb[3 * i] = r; rgb[3 * i + 1] = g; rgb[3 * i + 2] = b;
    }
    f.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
}

bool fileIsEmpty(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return !f || f.peek() == std::ifstream::traits_type::eof();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parseArgs(argc, argv);
        const Params& prm = o.params;

        std::printf("Image %dx%d, max %d iterations, %d run(s) per measurement, chunk %d\n\n", prm.width,
                    prm.height, prm.maxIter, o.runs, o.chunk);

        // ---- serial baseline ----
        std::vector<double> serialTimes;
        Image reference;
        for (int r = 0; r < o.runs; ++r) {
            RunResult res = runSerial(prm);
            serialTimes.push_back(res.seconds);
            if (r == 0) reference = std::move(res.image);
        }
        const double serialSec = median(serialTimes);
        if (!o.imagePath.empty()) {
            writePpm(o.imagePath, prm, reference);
            std::printf("Wrote %s\n", o.imagePath.c_str());
        }

        std::ofstream csv;
        if (!o.csvPath.empty()) {
            const bool header = fileIsEmpty(o.csvPath);
            csv.open(o.csvPath, std::ios::app);
            if (!csv) throw std::runtime_error("cannot write " + o.csvPath);
            if (header) csv << "strategy,width,height,iter,threads,chunk,seconds,speedup,efficiency,imbalance,verified\n";
            csv << "serial," << prm.width << "," << prm.height << "," << prm.maxIter << ",1," << o.chunk << ","
                << serialSec << ",1,1,1,1\n";
        }

        std::printf("%-15s %7s %10s %9s %11s %10s %9s\n", "strategy", "threads", "time (s)", "speedup",
                    "efficiency", "imbalance", "verified");
        std::printf("%-15s %7d %10.4f %9.2f %11.2f %10.2f %9s\n", "serial", 1, serialSec, 1.0, 1.0, 1.0, "-");

        bool allOk = true;
        for (const std::string& name : o.strategies) {
            for (int p : o.threads) {
                std::vector<double> times;
                std::vector<Sample> samples;
                bool ok = true;
                for (int r = 0; r < o.runs; ++r) {
                    RunResult res = runStrategy(name, prm, p, o.chunk);
                    if (o.verify && res.image != reference) ok = false;
                    times.push_back(res.seconds);
                    samples.push_back({res.seconds, res.threadBusy});
                }
                // Use the run whose time is closest to the median for the imbalance figure.
                const double med = median(times);
                std::size_t best = 0;
                for (std::size_t i = 1; i < samples.size(); ++i)
                    if (std::fabs(samples[i].seconds - med) < std::fabs(samples[best].seconds - med)) best = i;
                const double speedup = serialSec / med;
                const double eff = speedup / p;
                const double imb = imbalance(samples[best].busy);
                allOk = allOk && ok;

                std::printf("%-15s %7d %10.4f %9.2f %11.2f %10.2f %9s\n", name.c_str(), p, med, speedup, eff, imb,
                            o.verify ? (ok ? "yes" : "NO") : "skipped");
                if (csv.is_open())
                    csv << name << "," << prm.width << "," << prm.height << "," << prm.maxIter << "," << p << ","
                        << o.chunk << "," << med << "," << speedup << "," << eff << "," << imb << ","
                        << (ok ? 1 : 0) << "\n";
            }
        }

        if (!allOk) {
            std::fprintf(stderr, "\nERROR: at least one parallel image differs from the serial image.\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
