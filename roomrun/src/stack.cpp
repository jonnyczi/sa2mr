// Heights for a course whose decks overlap in plan (coils): a system of difference constraints.
//
// Unknowns: the deck height at each station. Constraints, each of the form x_v - x_u <= w:
//   * the slope from a station to the next within the walkable climb and descent on the inside of its bend
//     (GenParams::walkRise / walkFall times the bend's factor, times coilRiseSafety / coilFallSafety); the start line level;
//   * inside the height window, above any furniture top under the deck, within delta of the guide;
//   * every overlapping pair (Skeleton::partners) at least layerGapU + stackMarginU apart, the upper one
//     chosen per over/under region (a connected run of overlapping pairs).
// Feasibility is a negative-cycle test on the constraint graph (Bellman-Ford style, deterministic FIFO); the
// highest solution is the shortest distances from a reference node, the lowest the reversed graph's, and the
// profile is their middle (feasible: the set is convex). Which deck is on top in each region is searched
// depth-first (the guide's choice first), each partial choice tested for feasibility.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <numeric>

#include "roomrun/skeleton.hpp"

namespace rr {

double walkBendFactor(const GenParams& gp, double k) {
    const double hw = k > 0.03 ? gp.railHalfWidthU : 0.5 * gp.trackWidthU;   // tight bends: a rail strip carries the slope
    return std::max(0.05, 1.0 - k * hw);
}

namespace {

struct Edge { int to; double w; };

// Shortest distances from src (FIFO label correcting, deterministic order); false on a negative cycle.
bool shortest(const std::vector<std::vector<Edge>>& adj, int src, std::vector<double>& d) {
    const int V = (int)adj.size();
    d.assign(V, 1e18);
    d[src] = 0;
    std::vector<int> cnt(V, 0);
    std::vector<uint8_t> inq(V, 0);
    std::deque<int> q{src};
    inq[src] = 1;
    while (!q.empty()) {
        const int u = q.front();
        q.pop_front();
        inq[u] = 0;
        for (const Edge& e : adj[u]) {
            const double nd = d[u] + e.w;
            if (nd < d[e.to] - 1e-12) {
                d[e.to] = nd;
                if (!inq[e.to]) {
                    if (++cnt[e.to] > V + 2) return false;
                    q.push_back(e.to);
                    inq[e.to] = 1;
                }
            }
        }
    }
    return true;
}

struct Region { std::vector<std::pair<int, int>> pairs; bool guideFirstUp = true; };

}  // namespace

bool solveStackedProfile(Skeleton& sk, const Room& room, const GenParams& gp, const std::vector<double>& guide, int flatStations) {
    const int N = sk.size();
    if (N < 16 || (int)guide.size() != N) return false;
    const double mpu = gp.metersPerUnit();
    const double G = (gp.layerGapU + gp.stackMarginU) * mpu;
    const double hw = 0.5 * gp.trackWidthU * mpu;
    std::vector<double> lo(N, gp.minH), hi(N, gp.maxH);
    for (int j = 0; j < N; ++j)
        for (const Box& b : room.boxes) {
            if (b.height > gp.tallBoxM) continue;
            if (b.rect.dist(sk.pts[j]) < hw + gp.boxMarginM) lo[j] = std::max(lo[j], b.height + 0.01);
        }
    std::vector<double> up(N), dn(N);   // the slope limits of segment j -> j + 1 (m)
    for (int j = 0; j < N; ++j) {
        const int jn = (j + 1) % N;
        const double f = walkBendFactor(gp, std::max(std::fabs(sk.curv[j]), std::fabs(sk.curv[jn]))) * sk.stepM;
        const bool flat = j < flatStations;
        up[j] = flat ? 0.0 : gp.walkRise * gp.coilRiseSafety * f;
        dn[j] = flat ? 0.0 : gp.walkFall * gp.coilFallSafety * f;
    }
    // over/under regions: overlapping pairs (i < j) joined when both ends are within 2 stations
    std::vector<std::pair<int, int>> pairs;
    for (int j = 0; j < N; ++j)
        for (int p : sk.partners[j]) if (p > j) pairs.push_back({j, p});
    std::vector<int> parent(pairs.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> findp = [&](int a) { return parent[a] == a ? a : parent[a] = findp(parent[a]); };
    {
        std::vector<std::vector<int>> byI(N);
        for (size_t q = 0; q < pairs.size(); ++q) byI[pairs[q].first].push_back((int)q);
        auto cd = [&](int a, int b) { int d = std::abs(a - b); return std::min(d, N - d); };
        for (size_t q = 0; q < pairs.size(); ++q)
            for (int di = -2; di <= 2; ++di)
                for (int o : byI[((pairs[q].first + di) % N + N) % N])
                    if (cd(pairs[o].second, pairs[q].second) <= 2) parent[findp((int)q)] = findp(o);
    }
    std::vector<Region> regions;
    {
        std::vector<int> idx(pairs.size(), -1);
        for (size_t q = 0; q < pairs.size(); ++q) {
            const int rt = findp((int)q);
            if (idx[rt] < 0) { idx[rt] = (int)regions.size(); regions.push_back({}); }
            regions[idx[rt]].pairs.push_back(pairs[q]);
        }
        for (auto& rg : regions) {
            double vote = 0;
            for (auto& pr : rg.pairs) vote += guide[pr.first] - guide[pr.second];
            rg.guideFirstUp = vote >= 0;
        }
        // the biggest regions decided first
        std::stable_sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) { return a.pairs.size() > b.pairs.size(); });
    }
    if (regions.size() > 24) return false;   // too tangled to search
    const int R = N;
    auto build = [&](const std::vector<int>& orient, double delta, std::vector<std::vector<Edge>>* rev) {
        std::vector<std::vector<Edge>> fwd(N + 1);
        if (rev) rev->assign(N + 1, {});
        auto add = [&](int u, int v, double w) { fwd[u].push_back({v, w}); if (rev) (*rev)[v].push_back({u, w}); };   // x_v <= x_u + w
        for (int j = 0; j < N; ++j) {
            const double l = std::max(lo[j], guide[j] - delta), h = std::min(hi[j], guide[j] + delta);
            add(R, j, h);
            add(j, R, -l);
        }
        for (int j = 0; j < N; ++j) {
            const int jn = (j + 1) % N;
            add(j, jn, up[j]);
            add(jn, j, dn[j]);
        }
        for (size_t g = 0; g < orient.size(); ++g)
            for (auto& pr : regions[g].pairs) {
                if (orient[g] > 0) add(pr.first, pr.second, -G);   // first above: x_second <= x_first - G
                else add(pr.second, pr.first, -G);
            }
        return fwd;
    };
    // depth-first over the regions' orders, the guide's first; each partial choice tested (no guide band)
    std::vector<int> orient;
    int tests = 0;
    std::vector<double> d;
    std::function<bool()> dfs = [&]() -> bool {
        if (orient.size() == regions.size()) return true;
        const Region& rg = regions[orient.size()];
        for (int t = 0; t < 2; ++t) {
            const int o = (t == 0) == rg.guideFirstUp ? 1 : -1;
            orient.push_back(o);
            if (++tests > 96) { orient.pop_back(); return false; }
            if (shortest(build(orient, 10.0, nullptr), R, d) && dfs()) return true;
            orient.pop_back();
        }
        return false;
    };
    if (!dfs()) {
        if (gp.debug) {
            const bool alone = shortest(build({}, 10.0, nullptr), R, d);
            double worstUp = 0, worstDn = 0;   // the guide's own steepest climb and fall, against the limits
            for (int j = 0; j < N; ++j) {
                const double s = guide[(j + 1) % N] - guide[j];
                if (up[j] > 0) worstUp = std::max(worstUp, s / up[j]);
                if (dn[j] > 0) worstDn = std::max(worstDn, -s / dn[j]);
            }
            int firstReg = -1;
            if (!regions.empty()) firstReg = regions[0].pairs.front().first;
            int close = 0, flip = 0;
            double worst = 1e9;
            int wi = -1, wj = -1;
            for (auto& rg : regions)
                for (auto& pr : rg.pairs) {
                    const double dd = (guide[pr.first] - guide[pr.second]) * (rg.guideFirstUp ? 1 : -1);
                    close += dd < G;
                    flip += dd < 0;
                    if (dd < worst) { worst = dd; wi = pr.first; wj = pr.second; }
                }
            std::fprintf(stderr, "    stack solve: the guide has %d pairs under the gap, %d against its region's order; worst %.3f m at %d-%d (guide %.3f %.3f)\n",
                         close, flip, worst, wi, wj, wi >= 0 ? guide[wi] : 0.0, wj >= 0 ? guide[wj] : 0.0);
            std::fprintf(stderr, "    stack solve: no order of %zu over/under regions (%zu pairs) fits (%d tests); slopes and bounds alone %s; "
                         "the guide's steepest climb %.2f and fall %.2f of the limits; biggest region at station %d of %d (%zu pairs, to %d)\n",
                         regions.size(), pairs.size(), tests, alone ? "fit" : "do NOT fit", worstUp, worstDn, firstReg, N,
                         regions.empty() ? (size_t)0 : regions[0].pairs.size(), regions.empty() ? -1 : regions[0].pairs.front().second);
        }
        return false;
    }
    // the profile: as near the guide as the constraints allow
    for (double delta : {0.08, 0.2, 0.4, 10.0}) {
        std::vector<std::vector<Edge>> rev;
        const auto fwd = build(orient, delta, &rev);
        std::vector<double> dMax, dMin;
        bool ok = true;
        for (int j = 0; j < N && ok; ++j) ok = std::max(lo[j], guide[j] - delta) <= std::min(hi[j], guide[j] + delta);
        if (!ok || !shortest(fwd, R, dMax) || !shortest(rev, R, dMin)) continue;
        std::vector<double> z(N);
        for (int j = 0; j < N; ++j) z[j] = 0.5 * (dMax[j] - dMin[j]);
        sk.target = z;
        sk.solved = true;
        return true;
    }
    return false;
}

bool rollerProfile(Skeleton& sk, const Room& room, const GenParams& gp, Rng& rng) {
    const int N = sk.size();
    if (N < 32 || sk.solved) return false;
    const double mpu = gp.metersPerUnit();
    const double G = (gp.layerGapU + gp.stackMarginU) * mpu;
    const int flat = (int)std::ceil(44.0 / gp.stationU);
    std::vector<double> up(N), dn(N);
    for (int j = 0; j < N; ++j) {
        const double f = walkBendFactor(gp, std::max(std::fabs(sk.curv[j]), std::fabs(sk.curv[(j + 1) % N]))) * sk.stepM;
        up[j] = gp.walkRise * gp.coilRiseSafety * f * 0.97;
        dn[j] = gp.walkFall * gp.coilFallSafety * f * 0.97;
    }
    // where the low and high plateaus go: a crossing's passes if the loop crosses itself (the first pass high or
    // low at random), else the start line low and the high one across the lap; a long lap gets two hills
    std::vector<int> lowC, highC;
    int i0 = -1;
    for (int j = 0; j < N && i0 < 0; ++j) if (!sk.partners[j].empty()) i0 = j;
    int run = 0;   // the stacked run's half length (its plateaus cover it)
    if (i0 >= 0) {
        int j0 = sk.partners[i0][0];
        int a = i0;
        while (a + 1 < N && !sk.partners[a + 1].empty()) ++a;
        run = (a - i0) / 2 + 1;
        int mid = (i0 + a) / 2, other = sk.partners[mid].empty() ? j0 : sk.partners[mid][sk.partners[mid].size() / 2];
        if (rng.chance(0.5)) std::swap(mid, other);
        highC.push_back(mid);
        lowC.push_back(other);
    } else {
        const int hills = sk.lengthM() > 9.0 && rng.chance(0.6) ? 2 : 1;
        for (int h = 0; h < hills; ++h) {
            const int a = (flat / 2 + h * N / hills) % N;
            lowC.push_back(a);
            highC.push_back((a + (int)(N / hills * rng.range(0.5, 0.65))) % N);
        }
    }
    // stations: each plateau +- its half width; between a low and the next high the climb, then the descent
    std::vector<int> role(N, 0);   // 1 climb, -1 descent, 0 plateau
    std::vector<int> centres;
    for (size_t h = 0; h < lowC.size(); ++h) { centres.push_back(lowC[h]); centres.push_back(highC[h]); }
    std::vector<int> ord(centres.size());
    std::iota(ord.begin(), ord.end(), 0);
    std::sort(ord.begin(), ord.end(), [&](int x, int y) { return (centres[x] - lowC[0] + N) % N < (centres[y] - lowC[0] + N) % N; });
    const int plat = std::max(run + 2, (int)(N * rng.range(0.04, 0.1)));
    double capUp = 0, capDn = 0;
    for (size_t q = 0; q < ord.size(); ++q) {
        const int c0 = centres[ord[q]], c1 = centres[ord[(q + 1) % ord.size()]];
        const bool rising = ord[q] % 2 == 0;   // from a low centre to a high one
        int len = (c1 - c0 + N) % N;
        if (len == 0) len = N;
        for (int k = plat; k < len - plat; ++k) {
            const int j = (c0 + k) % N;
            role[j] = rising ? 1 : -1;
        }
    }
    for (int j = 0; j < N; ++j) { if (role[j] > 0) capUp += up[j]; if (role[j] < 0) capDn += dn[j]; }
    double lo = gp.minH + 0.05 + rng.range(0, 0.1);
    const double hi = gp.maxH - 0.03 - rng.range(0, 0.15);
    // each hill's height: what its climb and descent can make, up to the window (shared out by the walk factor)
    const double H = std::min({hi - lo, capUp / lowC.size(), capDn / lowC.size()});
    if (i0 >= 0 && H < G * 1.02) return false;   // the crossing cannot be a jump apart
    if (H < 0.15) return false;
    lo = std::max(lo, std::min(0.9 - 0.5 * H, hi - H));   // a small hill centred at 0.9 m: a course you look at, not at your feet
    std::vector<double> guide(N, lo);
    {
        // walk from the first low centre: plateaus hold, climbs rise H in proportion to up[], descents fall H by dn[]
        double h = lo;
        const int start = lowC[0];
        struct Run { int k0, len; double tot; };   // a climb or a descent in walk order: its first step, length, total limit
        std::vector<Run> runs;
        for (int k = 0; k < N; ++k) {
            const int j = (start + k) % N;
            if (role[j] == 0) continue;
            if (runs.empty() || role[(j + N - 1) % N] != role[j]) runs.push_back({k, 0, 0.0});
            runs.back().len++;
            runs.back().tot += role[j] > 0 ? up[j] : dn[j];
        }
        int climbs = 0, falls = 0;
        for (auto& rn : runs) (role[(start + rn.k0) % N] > 0 ? climbs : falls)++;
        if (climbs != falls || climbs == 0) return false;
        size_t r = 0;
        for (int k = 0; k < N; ++k) {
            const int j = (start + k) % N;
            guide[j] = h;
            if (role[j] == 0) continue;
            while (r + 1 < runs.size() && k >= runs[r].k0 + runs[r].len) ++r;
            h += (role[j] > 0 ? 1 : -1) * H * (role[j] > 0 ? up[j] : dn[j]) / std::max(1e-9, runs[r].tot);
        }
    }
    const std::vector<double> g = guide;
    if (!solveStackedProfile(sk, room, gp, g, flat)) return false;
    return true;
}

}  // namespace rr
