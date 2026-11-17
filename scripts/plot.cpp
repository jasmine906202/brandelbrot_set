// plot.cpp - C++ replacement for scripts/plot.py (no Python / matplotlib needed).
//
// Reads results/results.csv and results/chunks.csv (written by
// scripts/benchmark.sh) and writes SVG graphs:
//   results/speedup.svg, efficiency.svg, imbalance.svg, chunks.svg
// SVG is used because it needs no image library; open it in any browser.
//
// Build:  make plot      Run:  ./build/plot   (from the project root)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Row = std::map<std::string, std::string>;
using Pts = std::vector<std::pair<double, double>>;

static std::vector<Row> load(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "%s not found - run scripts/benchmark.sh first\n", path.c_str());
        std::exit(1);
    }
    std::vector<Row> rows;
    std::vector<std::string> head;
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) cells.push_back(c);
        if (first) { head = cells; first = false; continue; }
        // Benchmark appends several runs to one file; skip repeated headers.
        if (!cells.empty() && cells[0] == head[0]) continue;
        Row r;
        for (size_t i = 0; i < head.size() && i < cells.size(); ++i) r[head[i]] = cells[i];
        rows.push_back(r);
    }
    return rows;
}

static double num(const Row& r, const char* k) {
    auto it = r.find(k);
    return it == r.end() ? 0.0 : std::atof(it->second.c_str());
}
static std::string str(const Row& r, const char* k) {
    auto it = r.find(k);
    return it == r.end() ? "" : it->second;
}

struct Series { std::string name; Pts pts; bool dashed = false; };

static const char* kColors[] = {"#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b"};

// One chart panel drawn at (ox, oy) with size w x h.
static void panel(std::ostream& o, double ox, double oy, double w, double h,
                  const std::string& title, const std::string& xl, const std::string& yl,
                  const std::vector<Series>& ss, bool logx, bool legend) {
    const double L = 60, R = 15, T = 28, B = 42;
    const double pw = w - L - R, ph = h - T - B;
    double xmin = 1e300, xmax = -1e300, ymin = 0, ymax = -1e300;
    for (auto& s : ss) for (auto& p : s.pts) {
        xmin = std::min(xmin, p.first);  xmax = std::max(xmax, p.first);
        ymax = std::max(ymax, p.second);
    }
    if (xmin > xmax) return;
    if (xmin == xmax) xmax = xmin + 1;
    if (ymax <= 0) ymax = 1;
    ymax *= 1.05;
    auto X = [&](double x) {
        double t = logx ? (std::log2(x) - std::log2(xmin)) / (std::log2(xmax) - std::log2(xmin))
                        : (x - xmin) / (xmax - xmin);
        return ox + L + t * pw;
    };
    auto Y = [&](double y) { return oy + T + ph - (y - ymin) / (ymax - ymin) * ph; };

    o << "<text x='" << ox + w / 2 << "' y='" << oy + 18 << "' text-anchor='middle' font-size='14'>" << title << "</text>\n";
    // y grid + labels
    for (int i = 0; i <= 5; ++i) {
        double v = ymin + (ymax - ymin) * i / 5;
        o << "<line x1='" << ox + L << "' x2='" << ox + L + pw << "' y1='" << Y(v) << "' y2='" << Y(v)
          << "' stroke='#ddd'/><text x='" << ox + L - 6 << "' y='" << Y(v) + 4
          << "' text-anchor='end' font-size='11'>" << std::round(v * 100) / 100 << "</text>\n";
    }
    // x ticks at the distinct x values
    std::set<double> xs;
    for (auto& s : ss) for (auto& p : s.pts) xs.insert(p.first);
    for (double x : xs)
        o << "<line x1='" << X(x) << "' x2='" << X(x) << "' y1='" << oy + T + ph << "' y2='" << oy + T + ph + 4
          << "' stroke='#333'/><text x='" << X(x) << "' y='" << oy + T + ph + 16
          << "' text-anchor='middle' font-size='11'>" << x << "</text>\n";
    o << "<rect x='" << ox + L << "' y='" << oy + T << "' width='" << pw << "' height='" << ph
      << "' fill='none' stroke='#333'/>\n";
    o << "<text x='" << ox + L + pw / 2 << "' y='" << oy + h - 6 << "' text-anchor='middle' font-size='12'>" << xl << "</text>\n";
    o << "<text transform='translate(" << ox + 14 << "," << oy + T + ph / 2 << ") rotate(-90)' text-anchor='middle' font-size='12'>" << yl << "</text>\n";

    int ci = 0, li = 0;
    for (auto& s : ss) {
        const char* col = s.dashed ? "#000" : kColors[ci++ % 6];
        o << "<polyline fill='none' stroke='" << col << "' stroke-width='" << (s.dashed ? 1 : 2) << "'"
          << (s.dashed ? " stroke-dasharray='5,4'" : "") << " points='";
        for (auto& p : s.pts) o << X(p.first) << "," << Y(p.second) << " ";
        o << "'/>\n";
        if (!s.dashed)
            for (auto& p : s.pts)
                o << "<circle cx='" << X(p.first) << "' cy='" << Y(p.second) << "' r='3' fill='" << col << "'/>\n";
        if (legend) {
            double ly = oy + T + 14 + 16 * li++;
            o << "<line x1='" << ox + L + 8 << "' x2='" << ox + L + 28 << "' y1='" << ly - 4 << "' y2='" << ly - 4
              << "' stroke='" << col << "' stroke-width='2'" << (s.dashed ? " stroke-dasharray='5,4'" : "")
              << "/><text x='" << ox + L + 34 << "' y='" << ly << "' font-size='11'>" << s.name << "</text>\n";
        }
    }
}

static void writeSvg(const std::string& path, double W, double H, const std::string& body) {
    std::ofstream f(path);
    f << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H
      << "' viewBox='0 0 " << W << " " << H << "' font-family='sans-serif'>\n"
      << "<rect width='100%' height='100%' fill='white'/>\n" << body << "</svg>\n";
}

static void scalingPlots(const std::vector<Row>& all) {
    std::vector<Row> rows;
    for (auto& r : all) if (str(r, "strategy") != "serial") rows.push_back(r);

    std::vector<std::pair<int, int>> sizes;
    std::vector<std::string> strategies;
    std::set<double> threads;
    for (auto& r : rows) {
        std::pair<int, int> sz{(int)num(r, "width"), (int)num(r, "height")};
        if (std::find(sizes.begin(), sizes.end(), sz) == sizes.end()) sizes.push_back(sz);
        if (std::find(strategies.begin(), strategies.end(), str(r, "strategy")) == strategies.end())
            strategies.push_back(str(r, "strategy"));
        threads.insert(num(r, "threads"));
    }
    std::sort(sizes.begin(), sizes.end(), [](auto& a, auto& b) { return a.first * a.second < b.first * b.second; });

    struct M { const char* key; const char* label; };
    const M metrics[] = {{"speedup", "Speedup"}, {"efficiency", "Efficiency"},
                         {"imbalance", "Load imbalance (max / mean busy time)"}};
    for (auto& m : metrics) {
        const double pw = 480, ph = 360;
        std::ostringstream body;
        for (size_t i = 0; i < sizes.size(); ++i) {
            std::vector<Series> ss;
            for (auto& s : strategies) {
                Series se; se.name = s;
                for (auto& r : rows)
                    if (str(r, "strategy") == s && (int)num(r, "width") == sizes[i].first &&
                        (int)num(r, "height") == sizes[i].second)
                        se.pts.push_back({num(r, "threads"), num(r, m.key)});
                std::sort(se.pts.begin(), se.pts.end());
                if (!se.pts.empty()) ss.push_back(se);
            }
            if (std::string(m.key) == "speedup") {
                Series id; id.name = "ideal"; id.dashed = true;
                for (double t : threads) id.pts.push_back({t, t});
                ss.push_back(id);
            }
            if (std::string(m.key) == "efficiency") {
                Series id; id.name = "ideal"; id.dashed = true;
                for (double t : threads) id.pts.push_back({t, 1.0});
                ss.push_back(id);
            }
            panel(body, i * pw, 0, pw, ph, std::to_string(sizes[i].first) + "x" + std::to_string(sizes[i].second),
                  "threads", m.label, ss, false, i == 0);
        }
        writeSvg(std::string("results/") + m.key + ".svg", pw * sizes.size(), ph, body.str());
    }
}

static void chunkPlot(const std::vector<Row>& all) {
    std::vector<Row> rows;
    double pmax = 0;
    for (auto& r : all) if (str(r, "strategy") != "serial") { rows.push_back(r); pmax = std::max(pmax, num(r, "threads")); }
    std::vector<std::string> strategies;
    for (auto& r : rows)
        if (std::find(strategies.begin(), strategies.end(), str(r, "strategy")) == strategies.end())
            strategies.push_back(str(r, "strategy"));
    std::vector<Series> ss;
    for (auto& s : strategies) {
        Series se; se.name = s;
        for (auto& r : rows)
            if (str(r, "strategy") == s && num(r, "threads") == pmax) se.pts.push_back({num(r, "chunk"), num(r, "speedup")});
        std::sort(se.pts.begin(), se.pts.end());
        if (!se.pts.empty()) ss.push_back(se);
    }
    std::ostringstream body;
    panel(body, 0, 0, 600, 400, "Chunk size study", "chunk size (rows per grab, log2 scale)",
          "Speedup at " + std::to_string((int)pmax) + " threads", ss, true, true);
    writeSvg("results/chunks.svg", 600, 400, body.str());
}

int main() {
    scalingPlots(load("results/results.csv"));
    chunkPlot(load("results/chunks.csv"));
    std::puts("Wrote results/speedup.svg, efficiency.svg, imbalance.svg, chunks.svg");
}
