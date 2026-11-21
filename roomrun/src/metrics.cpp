#include "roomrun/metrics.hpp"

#include <algorithm>
#include <cmath>

namespace rr {

ShapeMetrics shapeMetrics(const GenResult& r, const PlayMesh& pm, double overlapM) {
    ShapeMetrics s;
    const int N = (int)pm.st.size();
    if (N < 3) return s;
    const double mpu = pm.mpu;
    double step = 0;
    for (int j = 0; j < N; ++j) {
        const Vec3 &a = pm.st[j].p, &b = pm.st[(j + 1) % N].p;
        step += std::hypot(b.x - a.x, b.z - a.z);
    }
    s.lapM = step;
    step /= N;
    s.floorM2 = std::fabs(signedArea(r.room.floor));
    s.lapPerM2 = s.floorM2 > 0 ? s.lapM / s.floorM2 : 0;
    s.minDeckM = 1e9; s.maxDeckM = -1e9;
    double sum = 0;
    for (auto& st : pm.st) { s.minDeckM = std::min(s.minDeckM, st.p.y); s.maxDeckM = std::max(s.maxDeckM, st.p.y); sum += st.p.y; }
    s.meanDeckM = sum / N;
    // overlapping stations: walkways closer than their half widths, far apart along the course
    std::vector<std::vector<int>> ov(N);
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            const double reach = pm.st[i].halfW + pm.st[j].halfW + overlapM;
            const double dx = pm.st[i].p.x - pm.st[j].p.x, dz = pm.st[i].p.z - pm.st[j].p.z;
            if (dx * dx + dz * dz >= reach * reach) continue;
            int d = j - i;
            d = std::min(d, N - d);
            if (d * step < 2.5 * reach) continue;   // the same deck a little further on
            ov[i].push_back(j);
            ov[j].push_back(i);
        }
    s.minStackGapU = 0;
    double minGap = 1e9;
    int stacked = 0;
    for (int i = 0; i < N; ++i) {
        if (ov[i].empty()) continue;
        ++stacked;
        std::vector<double> h{pm.st[i].p.y};
        for (int j : ov[i]) { h.push_back(pm.st[j].p.y); minGap = std::min(minGap, std::fabs(pm.st[j].p.y - pm.st[i].p.y)); }
        std::sort(h.begin(), h.end());
        int t = 1;
        for (size_t k = 1; k < h.size(); ++k) t += h[k] - h[k - 1] > 5 * mpu;
        s.tiers = std::max(s.tiers, t);
    }
    s.stackedShare = (double)stacked / N;
    if (minGap < 1e9) { s.minStackGapM = minGap; s.minStackGapU = minGap / mpu; }
    // crossings: runs of stations over a lower deck, a run continuing while the deck under it does
    auto lower = [&](int i) {
        std::vector<int> l;
        for (int j : ov[i]) if (pm.st[j].p.y < pm.st[i].p.y - 1e-6) l.push_back(j);
        return l;
    };
    int first = -1;
    for (int i = 0; i < N; ++i) if (lower(i).empty()) { first = i; break; }
    if (first < 0) { s.crossings = 1; return s; }
    std::vector<int> prev;
    for (int k = 1; k <= N; ++k) {
        const int i = (first + k) % N;
        const std::vector<int> cur = lower(i);
        if (!cur.empty()) {
            bool cont = false;
            for (int a : cur) for (int b : prev) { int d = std::abs(a - b); d = std::min(d, N - d); cont |= d <= 3; }
            if (!cont) ++s.crossings;
        }
        prev = cur;
    }
    return s;
}

}  // namespace rr
