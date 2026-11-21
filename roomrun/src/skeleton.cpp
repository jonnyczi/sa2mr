#include "roomrun/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace rr {

double centerlineClearance(const GenParams& gp) {
    return 0.5 * gp.trackWidthU * gp.metersPerUnit() + 0.02;  // half width + grid slack
}

// Resample closed polyline (with optional attached scalar) to uniform spacing.
static void resampleWithValues(const std::vector<Vec2>& pts, const std::vector<double>* vals, double step,
                               std::vector<Vec2>& outP, std::vector<double>* outV) {
    size_t n = pts.size();
    std::vector<double> cum(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) cum[i + 1] = cum[i] + (pts[(i + 1) % n] - pts[i]).len();
    double L = cum[n];
    int N = std::max(8, (int)std::round(L / step));
    outP.clear();
    if (outV) outV->clear();
    size_t seg = 0;
    for (int k = 0; k < N; ++k) {
        double s = L * k / N;
        while (seg + 1 < n && cum[seg + 1] < s) ++seg;
        double sl = cum[seg + 1] - cum[seg];
        double t = sl > 0 ? std::clamp((s - cum[seg]) / sl, 0.0, 1.0) : 0.0;
        outP.push_back(pts[seg] + (pts[(seg + 1) % n] - pts[seg]) * t);
        if (outV && vals) outV->push_back((*vals)[seg] + ((*vals)[(seg + 1) % n] - (*vals)[seg]) * t);
    }
}

static double windingAround(const std::vector<Vec2>& pts, Vec2 c) {
    double w = 0;
    for (size_t i = 0, n = pts.size(); i < n; ++i) {
        Vec2 a = pts[i] - c, b = pts[(i + 1) % n] - c;
        w += std::atan2(a.cross(b), a.dot(b));
    }
    return w / (2 * kPi);
}

bool finalizeSkeleton(Skeleton& sk, const std::vector<Vec2>& dense, const FreeSpace& fs, const GenParams& gp,
                      Rng* rng, bool smooth) {
    const double mpu = gp.metersPerUnit();
    sk.stepM = gp.stationU * mpu;
    const double need = centerlineClearance(gp);
    std::vector<double> denseT = sk.target;
    bool hasT = denseT.size() == dense.size();
    std::vector<Vec2> pts;
    std::vector<double> T;
    resampleWithValues(dense, hasT ? &denseT : nullptr, sk.stepM * 0.5, pts, hasT ? &T : nullptr);
    if (smooth) {
        for (int it = 0; it < 12; ++it) {
            std::vector<Vec2> nxt = pts;
            for (size_t i = 0, n = pts.size(); i < n; ++i) {
                Vec2 q = pts[i] * 0.5 + (pts[(i + n - 1) % n] + pts[(i + 1) % n]) * 0.25;
                double cq = fs.clearance.sample(q);
                if (cq >= need || cq >= fs.clearance.sample(pts[i])) nxt[i] = q;
            }
            pts.swap(nxt);
        }
    }
    std::vector<Vec2> st;
    std::vector<double> stT;
    resampleWithValues(pts, hasT ? &T : nullptr, sk.stepM, st, hasT ? &stT : nullptr);
    int N = (int)st.size();
    if (N < 16) return false;
    for (auto& p : st)
        if (fs.clearance.sample(p) < need - 0.012) return false;
    // orientation: random direction of travel
    if (rng && rng->chance(0.5)) {
        std::reverse(st.begin(), st.end());
        if (hasT) std::reverse(stT.begin(), stT.end());
    }
    // curvature (1/m), then convert to 1/unit
    std::vector<double> k(N);
    const int w = 2;
    for (int i = 0; i < N; ++i) {
        Vec2 a = st[(i - w + N) % N], b = st[i], c = st[(i + w) % N];
        double den = (b - a).len() * (c - b).len() * (c - a).len();
        k[i] = den > 1e-12 ? 2.0 * (b - a).cross(c - b) / den : 0.0;
    }
    std::vector<double> ks(N);
    for (int i = 0; i < N; ++i) ks[i] = (k[(i + N - 1) % N] + 2 * k[i] + k[(i + 1) % N]) * 0.25 * mpu;
    // start station: on the straightest stretch (the start line wants a run-up)
    int win = std::max(4, (int)(0.6 / sk.stepM));
    std::vector<std::pair<double, int>> straight;
    for (int i = 0; i < N; i += 2) {
        double s = 0;
        for (int j = -win / 3; j < win; ++j) s += std::fabs(ks[(i + j + N) % N]);
        straight.push_back({s, i});
    }
    std::sort(straight.begin(), straight.end());
    int pick = straight[rng ? rng->irange(0, std::min<int>(3, (int)straight.size() - 1)) : 0].second;
    if (sk.keepStart) pick = 0;   // the caller put the start line where it wants it (a tower: the level top before it)
    std::rotate(st.begin(), st.begin() + pick, st.end());
    std::rotate(ks.begin(), ks.begin() + pick, ks.end());
    if (hasT) std::rotate(stT.begin(), stT.begin() + pick, stT.end());
    sk.pts = st;
    sk.curv = ks;
    sk.target = hasT ? stT : std::vector<double>();
    // plan-view self-overlap (stacked layers): spatial hash
    double widthM = gp.trackWidthU * mpu;
    double rad = widthM + 0.04;
    double minArc = std::max(4 * widthM, 0.6);
    sk.partners.assign(N, {});
    std::unordered_map<int64_t, std::vector<int>> cells;
    auto key = [&](int cx, int cy) { return ((int64_t)cx << 32) ^ (int64_t)(uint32_t)cy; };
    for (int i = 0; i < N; ++i)
        cells[key((int)std::floor(st[i].x / rad), (int)std::floor(st[i].y / rad))].push_back(i);
    for (int i = 0; i < N; ++i) {
        int cx = (int)std::floor(st[i].x / rad), cy = (int)std::floor(st[i].y / rad);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                auto it = cells.find(key(cx + dx, cy + dy));
                if (it == cells.end()) continue;
                for (int j : it->second) {
                    if (j == i) continue;
                    int d = std::abs(i - j);
                    d = std::min(d, N - d);
                    if (d * sk.stepM < minArc) continue;
                    if ((st[i] - st[j]).len() < rad) sk.partners[i].push_back(j);
                }
            }
    }
    return true;
}

// ------------------------------------------------------------------ candidates

static Skeleton makePerimeter(const std::vector<Vec2>& loop, const Room& room) {
    Skeleton sk;
    sk.kind = "perimeter";
    sk.aroundPlayer = std::fabs(windingAround(loop, room.player)) > 0.5;
    return sk;
}

static std::vector<Vec2> sampleCircle(Vec2 c, double r, double step) {
    std::vector<Vec2> out;
    int n = std::max(16, (int)(2 * kPi * r / step));
    for (int i = 0; i < n; ++i) {
        double a = 2 * kPi * i / n;
        out.push_back(c + Vec2{std::cos(a), std::sin(a)} * r);
    }
    return out;
}

static bool ringClear(const FreeSpace& fs, Vec2 c, double r, double need) {
    for (auto& p : sampleCircle(c, r, 0.02))
        if (fs.clearance.sample(p) < need) return false;
    return true;
}

// Crossed-belt figure-8 around two circles; empty if circles too close.
static std::vector<Vec2> figure8Path(Vec2 cA, double rA, Vec2 cB, double rB, double step) {
    std::vector<Vec2> out;
    Vec2 d = cB - cA;
    double D = d.len();
    if (D <= rA + rB + 1e-6) return out;
    Vec2 u = d / D, w = u.perp();
    double ca = (rA + rB) / D, sa = std::sqrt(std::max(0.0, 1 - ca * ca));
    Vec2 nP = u * ca + w * sa, nM = u * ca - w * sa;  // unit normals on A for the two internal tangents
    Vec2 tAp = cA + nP * rA, tBp = cB - nP * rB;
    Vec2 tAm = cA + nM * rA, tBm = cB - nM * rB;
    auto line = [&](Vec2 a, Vec2 b) {
        int n = std::max(2, (int)((b - a).len() / step));
        for (int i = 0; i < n; ++i) out.push_back(a + (b - a) * ((double)i / n));
    };
    auto arc = [&](Vec2 c, double r, Vec2 from, Vec2 to, Vec2 dirIn) {
        double a0 = std::atan2(from.y - c.y, from.x - c.x), a1 = std::atan2(to.y - c.y, to.x - c.x);
        Vec2 ccwTan = Vec2{-std::sin(a0), std::cos(a0)};
        bool ccw = ccwTan.dot(dirIn) > 0;
        double sweep = ccw ? a1 - a0 : a0 - a1;
        while (sweep <= 0) sweep += 2 * kPi;
        int n = std::max(4, (int)(sweep * r / step));
        for (int i = 0; i < n; ++i) {
            double a = a0 + (ccw ? 1 : -1) * sweep * i / n;
            out.push_back(c + Vec2{std::cos(a), std::sin(a)} * r);
        }
    };
    line(tAp, tBp);
    arc(cB, rB, tBp, tBm, (tBp - tAp).norm());
    line(tBm, tAm);
    arc(cA, rA, tAm, tAp, (tAm - tBm).norm());
    return out;
}

static bool pathClear(const FreeSpace& fs, const std::vector<Vec2>& path, double need) {
    for (auto& p : path)
        if (fs.clearance.sample(p) < need) return false;
    return !path.empty();
}

std::vector<Skeleton> skeletonCandidates(const Room& room, const FreeSpace& fs, const GenParams& gp, Rng& rng) {
    std::vector<Skeleton> out;
    const double mpu = gp.metersPerUnit();
    const double need = centerlineClearance(gp);
    const double widthM = gp.trackWidthU * mpu;
    const double minLoop = 1.5;

    // (a) inset perimeter contours of the opened free space
    const double extras[] = {0.0, 0.15, 0.35};
    const double radii[] = {0.22, 0.38};
    double bestPerimeter = 0;
    for (double ex : extras)
        for (double r : radii) {
            Grid G = openedField(fs.clearance, need + ex + 0.01, r);
            auto loops = contourLoops(G, 0.0);
            std::sort(loops.begin(), loops.end(), [](auto& a, auto& b) { return polylineLength(a, true) > polylineLength(b, true); });
            for (size_t li = 0; li < loops.size() && li < 2; ++li) {
                double L = polylineLength(loops[li], true);
                if (L < minLoop) continue;
                Skeleton sk = makePerimeter(loops[li], room);
                if (!finalizeSkeleton(sk, loops[li], fs, gp, &rng, true)) continue;
                bool overlaps = false;
                for (auto& p : sk.partners) if (!p.empty()) { overlaps = true; break; }
                if (overlaps) continue;
                sk.prelim = std::min(sk.lengthM(), 22.0) + (sk.aroundPlayer ? 1.5 : 0) - ex * 2 + rng.range(0, 2);
                bestPerimeter = std::max(bestPerimeter, sk.lengthM());
                out.push_back(std::move(sk));
            }
        }

    // disks for figure-8 lobes / helix centers: medial vertices + the player
    struct Disk { Vec2 c; double rMin, rMax; bool player; };
    std::vector<Disk> disks;
    const double lobeMin = std::max(0.22, 22 * mpu);
    for (size_t i = 0; i < fs.maxima.size() && disks.size() < 6; ++i) {
        double rmax = std::min(1.1, fs.maxima[i].r - need - 0.01);
        if (rmax >= lobeMin) disks.push_back({fs.maxima[i].p, lobeMin, rmax, false});
    }
    {
        double r0 = room.playerRadius + need + 0.01, r = r0, rOk = -1;
        while (r < r0 + 1.2 && ringClear(fs, room.player, r, need)) { rOk = r; r += 0.03; }
        if (rOk > 0) disks.push_back({room.player, r0, rOk, true});
    }

    // (b) figure-8 via medial-axis disks
    int f8 = 0;
    for (size_t a = 0; a < disks.size(); ++a)
        for (size_t b = a + 1; b < disks.size(); ++b) {
            const Disk &A = disks[a], &B = disks[b];
            double D = (A.c - B.c).len();
            for (double shrink : {1.0, 0.8, 0.62}) {
                double rA = std::max(A.rMin, A.rMax * shrink), rB = std::max(B.rMin, B.rMax * shrink);
                if (A.player) rA = A.rMin + (A.rMax - A.rMin) * (1 - shrink);
                if (B.player) rB = B.rMin + (B.rMax - B.rMin) * (1 - shrink);
                if (D < rA + rB + widthM * 1.5) continue;
                auto path = figure8Path(A.c, rA, B.c, rB, 0.01);
                if (!pathClear(fs, path, need)) continue;
                Skeleton sk;
                sk.kind = "figure8";
                sk.aroundPlayer = A.player || B.player;
                if (!finalizeSkeleton(sk, path, fs, gp, &rng, false)) continue;
                sk.prelim = std::min(sk.lengthM(), 22.0) + 2.5 + (sk.aroundPlayer ? 1.0 : 0) + rng.range(0, 2);
                out.push_back(std::move(sk));
                ++f8;
                break;
            }
        }

    // (c) concentric double helix (inner ring climbs, outer ring descends)
    const double gapM = gp.layerGapU * mpu + 0.04;
    const double lo = gp.prefMinH + 0.02, hi = gp.maxH - 0.1;
    for (auto& d : disks) {
        // Wide ring spacing keeps the inner<->outer transitions clear of both rings, so only
        // full-revolution layers stack; narrow spacing also stacks half-revolution offsets.
        const double rMinAbs = std::max(0.2, 22 * mpu), rad = widthM + 0.04;
        double r2 = d.rMax, r1 = r2 - (2 * rad + 0.03);
        double perRevNeed = gapM + 0.03;
        if (r1 < d.rMin || r1 < rMinAbs) {
            r1 = r2 - (widthM + 0.08);
            perRevNeed = 2 * gapM + 0.06;
        }
        if (r1 < d.rMin || r1 < rMinAbs) continue;
        bool overFurniture = false;  // helices wind over free floor only
        for (auto& b : room.boxes) if (b.rect.dist(d.c) < r2 + widthM) overFurniture = true;
        if (overFurniture) continue;
        double revs = 2.0;
        while (revs > 0.9 && (hi - lo) / revs < perRevNeed) revs -= 0.5;
        if (revs < 0.9) continue;
        double total = (2 * revs + 1) * 2 * kPi;
        double aIn = revs * 2 * kPi, aOut0 = aIn + kPi, aOut1 = aOut0 + revs * 2 * kPi;
        std::vector<Vec2> path;
        Skeleton sk;
        sk.kind = "helix";
        sk.aroundPlayer = d.player;
        double step = 0.01;
        double a = 0;
        double phase = rng.range(0, 2 * kPi);
        while (a < total) {
            double r, h;
            if (a < aIn) { r = r1; h = lo + (hi - lo) * a / aIn; }
            else if (a < aOut0) { double t = (a - aIn) / kPi; r = r1 + (r2 - r1) * (0.5 - 0.5 * std::cos(kPi * t)); h = hi; }
            else if (a < aOut1) { r = r2; h = hi - (hi - lo) * (a - aOut0) / (aOut1 - aOut0); }
            else { double t = (a - aOut1) / kPi; r = r2 + (r1 - r2) * (0.5 - 0.5 * std::cos(kPi * t)); h = lo; }
            path.push_back(d.c + Vec2{std::cos(a + phase), std::sin(a + phase)} * r);
            sk.target.push_back(h);
            a += step / r;
        }
        if (!pathClear(fs, path, need)) continue;
        if (!finalizeSkeleton(sk, path, fs, gp, nullptr, false)) continue;
        sk.prelim = std::min(sk.lengthM(), 22.0) + (bestPerimeter < 7.0 ? 4.0 : -4.0) + rng.range(0, 2);
        out.push_back(std::move(sk));
    }

    // (d) small rooms: a circle in the biggest free disk still makes a (vertical) course
    if (bestPerimeter < 4.0 && !fs.maxima.empty()) {
        double r = std::min(0.9, fs.maxima[0].r - need - 0.01);
        if (r >= 0.12 && 2 * kPi * r >= 1.5) {
            Skeleton sk;
            sk.kind = "circle";
            if (finalizeSkeleton(sk, sampleCircle(fs.maxima[0].p, r, 0.01), fs, gp, &rng, false)) {
                sk.prelim = sk.lengthM() + rng.range(0, 1);
                out.push_back(std::move(sk));
            }
        }
    }
    // roller-coaster profiles for most plain loops (all of the height window: stack.cpp), in small rooms nearly all;
    // coils and towers. Only with a library that can follow them (GenParams::vertical: constant-slope modules)
    if (gp.vertical) {
        const double p = bestPerimeter < 8.0 ? 0.8 : 0.55;
        for (auto& s : out)
            if ((s.kind == "perimeter" || s.kind == "figure8" || s.kind == "circle") && rng.chance(p)) {
                Skeleton v = s;
                if (rollerProfile(v, room, gp, rng)) s = std::move(v);
            }
    }
    // (e) coils: stacked lobes that use the height (coil.cpp)
    if (gp.vertical) coilCandidates(room, fs, gp, rng, out);
    // (f) towers: spiral towers hung off the loops above (tower.cpp)
    if (gp.vertical) {
        const std::vector<Skeleton> bases = out;
        towerCandidates(room, fs, gp, rng, bases, out);
    }
    // every course within the game's collision budget: at most 860 stations (one deck lane, 8 triangles a station,
    // under PlayMeshParams::maxCollisionTriangles; 27 pieces of decks and 27 of walls, under 64) and 26 m a lap
    out.erase(std::remove_if(out.begin(), out.end(), [](const Skeleton& s) { return s.size() > 860 || s.lengthM() > 26.0; }), out.end());
    std::stable_sort(out.begin(), out.end(), [](const Skeleton& a, const Skeleton& b) { return a.prelim > b.prelim; });
    // keep the best of each kind first, then fill by score
    std::vector<Skeleton> picked;
    for (const char* k : {"perimeter", "figure8", "helix", "circle", "coil", "tower"})
        for (auto& s : out)
            if (s.kind == k) { picked.push_back(s); break; }
    std::stable_sort(picked.begin(), picked.end(), [](const Skeleton& a, const Skeleton& b) { return a.prelim > b.prelim; });
    for (auto& s : out) {
        if ((int)picked.size() >= gp.maxCandidates) break;
        bool dup = false;
        for (auto& p : picked) if (&p != &s && p.kind == s.kind && std::fabs(p.lengthM() - s.lengthM()) < 0.3) dup = true;
        if (!dup) picked.push_back(s);
    }
    if ((int)picked.size() > gp.maxCandidates) picked.resize(gp.maxCandidates);
    return picked;
}

bool fallbackSkeleton(const Room& room, const FreeSpace& fs, const GenParams& gp, Skeleton& out) {
    const double need = centerlineClearance(gp);
    for (double r : {0.3, 0.2}) {
        Grid G = openedField(fs.clearance, need + 0.01, r);
        auto loops = contourLoops(G, 0.0);
        std::sort(loops.begin(), loops.end(), [](auto& a, auto& b) { return polylineLength(a, true) > polylineLength(b, true); });
        for (auto& l : loops) {
            if (polylineLength(l, true) < 1.5) continue;
            Skeleton sk = makePerimeter(l, room);
            if (!finalizeSkeleton(sk, l, fs, gp, nullptr, true)) continue;
            bool overlaps = false;
            for (auto& p : sk.partners) if (!p.empty()) overlaps = true;
            if (overlaps) continue;
            out = sk;
            return true;
        }
    }
    // minimal circle in the largest free disk (rails handle radii down to ~8 units)
    if (!fs.maxima.empty()) {
        double r = std::min(0.8, fs.maxima[0].r - need - 0.01);
        if (r >= 10 * gp.metersPerUnit() && 2 * kPi * r >= 1.5) {
            Skeleton sk;
            sk.kind = "circle";
            if (finalizeSkeleton(sk, sampleCircle(fs.maxima[0].p, r, 0.01), fs, gp, nullptr, false)) {
                sk.aroundPlayer = false;
                out = sk;
                return true;
            }
        }
    }
    return false;
}

}  // namespace rr
