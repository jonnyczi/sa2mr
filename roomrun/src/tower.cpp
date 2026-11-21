// Towers: a loop (an inset perimeter, the user's ring, a figure-8, a circle) with one to three spiral towers
// hung off it. At a tower the course leaves the loop along its tangent, winds one or two full turns down a
// tight spiral beside it and comes back onto the loop where it left, a jump's height (or two) lower; the loop
// then climbs back gently to the next tower. Small rooms get decks stacked two or three high where only a
// flat loop fitted before, at the price of about 2 m of gentle climb a tower. Heights by solveStackedProfile.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "roomrun/skeleton.hpp"

namespace rr {

namespace {

bool circleClear(const FreeSpace& fs, Vec2 c, double r, double need) {
    const int n = std::max(16, (int)(2 * kPi * r / 0.02));
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPi * i / n;
        if (fs.clearance.sample(c + Vec2{std::cos(a), std::sin(a)} * r) < need - 0.004) return false;
    }
    return true;
}

struct Site { int i; int side; double r; };   // a tower at base station i, its centre on the left (+1) or right (-1)

}  // namespace

void towerCandidates(const Room& room, const FreeSpace& fs, const GenParams& gp, Rng& rng, const std::vector<Skeleton>& bases,
                     std::vector<Skeleton>& out) {
    const double mpu = gp.metersPerUnit();
    const double need = centerlineClearance(gp);
    const double G = (gp.layerGapU + gp.stackMarginU) * mpu;
    const double rLo = std::max(0.17, 14 * mpu), rHi = 0.7;
    const double window = gp.maxH - gp.minH - 0.1;
    const int flat = (int)std::ceil(44.0 / gp.stationU);
    // the fall one turn of a tower of radius r can make at its walkable slope
    auto turnFall = [&](double r) { return gp.walkFall * walkBendFactor(gp, mpu / r) * gp.coilFallSafety * 2 * kPi * r; };
    const double climb = gp.walkRise * gp.coilRiseSafety * 0.8;   // the loop's climb back (its bends cost some)
    const double rad = gp.trackWidthU * mpu + 0.04;   // decks closer than this overlap (Skeleton::partners)
    // A tower hugs its loop by the tangent point: within acos(1 - rad / r) of it on either side, a turn's deck is
    // over the loop's exit and under its entry, so there it must stay a gap above the exit and a gap below the
    // entry. Its fall D (entry to exit) is then at least two gaps less what the free middle of the turn can fall,
    // and at most what the whole turn can: the fall a one-turn tower of radius r needs, or -1 if it cannot.
    auto towerFall = [&](double r) {
        const double near = std::acos(std::max(-1.0, 1.0 - rad / r));
        const double perM = turnFall(r) / (2 * kPi * r);
        const double freeFall = perM * r * (2 * kPi - 2 * near);
        const double D = std::max(G * 1.05, 2 * G * 1.05 - 0.95 * freeFall);
        return D <= 0.95 * turnFall(r) ? D : -1.0;
    };
    std::vector<Vec2> base;
    // a tower must stay off the loop but near its own site: there the loop is mid-climb, and no tower height clears it
    auto offLoop = [&](Vec2 c, double r, int i) {
        const int N = (int)base.size();
        const double step = (base[1] - base[0]).len();
        const int keep = (int)std::ceil((r + rad) / step);
        for (int k = 0; k < N; ++k) {
            int d = std::abs(k - i);
            d = std::min(d, N - d);
            if (d <= keep) continue;
            if ((base[k] - c).len() < r + rad) return false;
        }
        return true;
    };
    std::vector<Skeleton> found;
    int tried = 0, noSite = 0, noProfile = 0;
    // the loops to hang towers on: the given ones and the user's ring (Sonic around you; its corridor beside the
    // user can be too narrow for the perimeter's rounded contours, and towers fit in the corners outside it)
    std::vector<Skeleton> loops = bases;
    {
        const double r0 = room.playerRadius + need + 0.01;
        double r = r0, rOk = -1;
        while (r < r0 + 1.2 && circleClear(fs, room.player, r, need + 0.004)) { rOk = r; r += 0.02; }
        std::vector<double> radii;   // the tightest ring leaves the most room for towers in the corners
        if (rOk > 0)
            for (double rr : {r0, r0 + 0.04, 0.5 * (r0 + rOk), rOk})
                if (rr <= rOk && (radii.empty() || rr > radii.back() + 0.03)) radii.push_back(rr);
        for (double rr : radii) {
            if (2 * kPi * rr < 1.5) continue;
            std::vector<Vec2> ring;
            const int n = std::max(32, (int)(2 * kPi * rr / 0.01));
            for (int i = 0; i < n; ++i) ring.push_back(room.player + Vec2{std::cos(2 * kPi * i / n), std::sin(2 * kPi * i / n)} * rr);
            Skeleton sk;
            sk.kind = "ring";
            sk.aroundPlayer = true;
            if (finalizeSkeleton(sk, ring, fs, gp, nullptr, false)) loops.push_back(std::move(sk));
        }
    }
    for (const Skeleton& b0 : loops) {
        if (b0.kind == "coil" || b0.kind == "helix" || b0.kind == "tower" || b0.size() < 24) continue;
        for (int trial = 0; trial < 6; ++trial) {
            ++tried;
            base = b0.pts;
            if (rng.chance(0.5)) std::reverse(base.begin(), base.end());
            const int N = (int)base.size();
            const double step = b0.stepM, Lb = N * step;
            // every 3rd station: the largest tower that fits on either side and can fall a gap in a turn
            std::vector<Site> sites;
            for (int i = 0; i < N; i += 3) {
                const Vec2 t = (base[(i + 1) % N] - base[(i + N - 1) % N]).norm(), n = t.perp();
                Site best{i, 0, 0};
                for (int side : {1, -1}) {
                    double ok = 0;
                    for (double r = rLo; r <= rHi; r += 0.02) {
                        if (!circleClear(fs, base[i] + n * (side * r), r, need) || !offLoop(base[i] + n * (side * r), r, i)) break;
                        ok = r;
                    }
                    if (ok > best.r) best = {i, side, ok};
                }
                if (best.r > 0 && turnFall(best.r) >= G * 1.05) sites.push_back(best);
            }
            if (gp.debug) {
                double rbest = 0;
                for (int i = 0; i < N; i += 3) for (int side : {1, -1}) {
                    const Vec2 t = (base[(i + 1) % N] - base[(i + N - 1) % N]).norm(), n = t.perp();
                    for (double r = rLo; r <= rHi; r += 0.02) { if (!circleClear(fs, base[i] + n * (side * r), r, need)) break; rbest = std::max(rbest, r); }
                }
                std::fprintf(stderr, "  tower sites on %s %.1f m: %zu (largest clear circle %.2f m without the loop check; a turn falls the gap from r %.2f)\n", b0.kind.c_str(), Lb, sites.size(), rbest, [&]{ double r = rLo; while (r < 1 && towerFall(r) < 0) r += 0.01; return r; }());
            }
            if (sites.empty()) { ++noSite; break; }   // no tower fits this loop
            // up to three towers, each with the climb back after it (spaced along the loop)
            const int want = rng.irange(1, 3);
            std::vector<Site> pick;
            std::vector<int> turns;
            for (int t = 0; t < 12 && (int)pick.size() < want; ++t) {
                Site s = sites[rng.irange(0, (int)sites.size() - 1)];
                double rFeas = rLo;   // the tightest tower that still falls its gap in a turn
                while (rFeas < s.r && towerFall(rFeas) < 0) rFeas += 0.01;   // the estimate (if the site has room for it)
                if (towerFall(rFeas) < 0) { rFeas = rLo; while (rFeas < s.r && turnFall(rFeas) < G * 1.05) rFeas += 0.01; }   // else the solver judges
                s.r = std::min(s.r, rFeas + (s.r - rFeas) * rng.range(0.0, 1.0));
                bool far = true;
                for (auto& p : pick) {
                    int d = std::abs(p.i - s.i);
                    d = std::min(d, N - d);
                    if (d * step < G / climb + 0.4) far = false;
                    if ((base[p.i] + (base[(p.i + 1) % N] - base[(p.i + N - 1) % N]).norm().perp() * (p.side * p.r) - base[s.i] - (base[(s.i + 1) % N] - base[(s.i + N - 1) % N]).norm().perp() * (s.side * s.r)).len() < p.r + s.r + rad) far = false;   // towers apart
                }
                if (!far) continue;
                const int tw = (2 * G * 1.05 <= window && rng.chance(0.4)) ? 2 : 1;   // two turns where two gaps fit the window
                pick.push_back(s);
                turns.push_back(tw);
            }
            if (pick.empty()) continue;
            std::vector<int> ord(pick.size());
            for (size_t q = 0; q < ord.size(); ++q) ord[q] = (int)q;
            std::sort(ord.begin(), ord.end(), [&](int a, int b) { return pick[a].i < pick[b].i; });
            // each tower falls its turns' gaps; the loop climbs it back before the next one (else the tower drops)
            std::vector<double> fallH(pick.size());
            double total = 0;
            for (size_t q = 0; q < pick.size(); ++q) {
                const double tf = G * 1.05;   // the guide: a gap a turn (the solver adds what the hug by the loop needs)
                fallH[q] = turns[q] == 1 ? tf : std::max(2 * G * 1.05, tf + G * 1.05);
                total += fallH[q];
            }
            if (total / climb + 0.6 > Lb) continue;
            double maxFall = 0;
            for (double f : fallH) maxFall = std::max(maxFall, f);
            if (maxFall > window) continue;
            // the path and its guide: the loop's stations, a tower's turns spliced in after its station
            std::vector<Vec2> path;
            std::vector<double> guide;
            const double top = std::min(gp.maxH - 0.04, gp.minH + 0.05 + maxFall + rng.range(0.0, std::max(0.0, window - maxFall)));
            // the loop's heights: from each tower's foot back up to the top before the next tower
            std::vector<double> hb(N, top);
            for (size_t q = 0; q < ord.size(); ++q) {
                const Site& a = pick[ord[q]];
                const Site& b = pick[ord[(q + 1) % ord.size()]];
                int len = (b.i - a.i + N) % N;
                if (len == 0) len = N;
                // level at the top before the next tower (before the first one, long enough for the start line)
                const double plateau = q + 1 == ord.size() ? (flat + 2) * step : 0.3;
                for (int k = 1; k <= len; ++k) {
                    const double f = std::min(1.0, (k * step) / std::max(step, len * step - plateau));
                    hb[(a.i + k) % N] = top - fallH[ord[q]] * (1.0 - f);
                }
            }
            // the start line on the level top before the first tower: the climb keeps the rest of the loop
            const int startBase = (pick[ord[0]].i - (flat + 1) + N) % N;
            size_t startAt = 0;
            int nextTower = 0;
            for (int i = 0; i < N; ++i) {
                if (i == startBase) startAt = path.size();
                path.push_back(base[i]);
                guide.push_back(hb[i]);
                if (nextTower < (int)ord.size() && pick[ord[nextTower]].i == i) {
                    const Site& s = pick[ord[nextTower]];
                    const Vec2 t = (base[(i + 1) % N] - base[(i + N - 1) % N]).norm(), n = t.perp();
                    const Vec2 c = base[i] + n * (s.side * s.r);
                    const double a0 = std::atan2(base[i].y - c.y, base[i].x - c.x);
                    const double sweep = 2 * kPi * turns[ord[nextTower]];
                    const int na = std::max(16, (int)(sweep * s.r / 0.01));
                    for (int q = 1; q < na; ++q) {
                        const double a = a0 + s.side * sweep * q / na;
                        path.push_back(c + Vec2{std::cos(a), std::sin(a)} * s.r);
                        guide.push_back(top - fallH[ord[nextTower]] * q / na);
                    }
                    // back at the loop, the tower's height lower
                    path.push_back(base[i] + t * 0.004);
                    guide.push_back(top - fallH[ord[nextTower]]);
                    ++nextTower;
                }
            }
            std::rotate(path.begin(), path.begin() + startAt, path.end());
            std::rotate(guide.begin(), guide.begin() + startAt, guide.end());
            Skeleton sk;
            sk.kind = "tower";
            sk.aroundPlayer = b0.aroundPlayer;
            sk.keepStart = true;
            sk.target = guide;
            if (!finalizeSkeleton(sk, path, fs, gp, nullptr, false)) continue;
            if (sk.lengthM() > 26.0) continue;
            const std::vector<double> g2 = sk.target;
            if (!solveStackedProfile(sk, room, gp, g2, flat)) { ++noProfile; continue; }
            const int M = sk.size();
            double zmin = 1e9, zmax = -1e9;
            int stacked = 0, tiers = 1;
            for (int j = 0; j < M; ++j) {
                zmin = std::min(zmin, sk.target[j]);
                zmax = std::max(zmax, sk.target[j]);
                if (sk.partners[j].empty()) continue;
                ++stacked;
                std::vector<double> h{sk.target[j]};
                for (int p : sk.partners[j]) h.push_back(sk.target[p]);
                std::sort(h.begin(), h.end());
                int tt = 1;
                for (size_t q = 1; q < h.size(); ++q) tt += h[q] - h[q - 1] > 0.5 * gp.layerGapU * mpu;
                tiers = std::max(tiers, tt);
            }
            const double share = (double)stacked / M;
            sk.prelim = std::min(sk.lengthM(), 22.0) + 4 * (zmax - zmin) + 3 * (tiers - 1) + 3 * share + rng.range(0, 2);
            if (gp.debug)
                std::fprintf(stderr, "  tower on %s %.1f m: %zu towers, %.1f m, heights %.2f..%.2f, %d tiers, %.0f%% stacked, prelim %.1f\n",
                             b0.kind.c_str(), b0.lengthM(), pick.size(), sk.lengthM(), zmin, zmax, tiers, share * 100, sk.prelim);
            found.push_back(std::move(sk));
        }
    }
    if (gp.debug) std::fprintf(stderr, "  towers: %zu of %d trials (no site %d, no profile %d)\n", found.size(), tried, noSite, noProfile);
    std::stable_sort(found.begin(), found.end(), [](const Skeleton& a, const Skeleton& b) { return a.prelim > b.prelim; });
    std::vector<const Skeleton*> keep;
    for (auto& f : found) {
        if ((int)keep.size() >= gp.coilKeep) break;
        bool dup = false;
        for (auto* q : keep) dup |= std::fabs(q->lengthM() - f.lengthM()) < 0.3;
        if (!dup) keep.push_back(&f);
    }
    for (auto* q : keep) out.push_back(*q);
}

}  // namespace rr
