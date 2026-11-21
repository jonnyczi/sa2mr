// Coils: courses that use the room's height. A chain of circular lobes in free disks (the user's ring among
// them), each wound up to two extra times and joined by tangent lines, climbing on some lobes and descending on
// others, so that decks stack: a spiral tower that winds down around itself, a figure-8 whose belts cross at
// different heights, a climb around the user and a drop back down. The heights come from a small linear
// program (difference constraints) that keeps every stacked pair a jump and Sonic apart and every slope
// walkable on the inside of its bend; the assembler then follows them with constant-slope spiral modules.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "roomrun/skeleton.hpp"

namespace rr {

namespace {

bool ringClear(const FreeSpace& fs, Vec2 c, double r, double need) {
    const int n = std::max(16, (int)(2 * kPi * r / 0.02));
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPi * i / n;
        if (fs.clearance.sample(c + Vec2{std::cos(a), std::sin(a)} * r) < need) return false;
    }
    return true;
}

// The line leaving circle 1 (centre c1, signed radius q1: + counter-clockwise) along its tangent and arriving
// along circle 2's: n . (c2 - c1) = q2 - q1 for its left normal n, the points c - n q.
bool tangentLine(Vec2 c1, double q1, Vec2 c2, double q2, Vec2& p1, Vec2& p2) {
    const Vec2 d = c2 - c1;
    const double D = d.len();
    if (D < 1e-6) return false;
    const double a = (q2 - q1) / D;
    if (std::fabs(a) > 0.995) return false;
    const Vec2 dh = d / D;
    const Vec2 n = dh * a + dh.perp() * std::sqrt(1 - a * a);
    p1 = c1 - n * q1;
    p2 = c2 - n * q2;
    return (p2 - p1).len() > 0.02;
}


}  // namespace


void coilCandidates(const Room& room, const FreeSpace& fs, const GenParams& gp, Rng& rng, std::vector<Skeleton>& out) {
    const double mpu = gp.metersPerUnit();
    const double need = centerlineClearance(gp);
    const double rLo = std::max(0.17, 14 * mpu);   // the tightest lobe (a rail strip's bend)
    struct Disk { Vec2 c; double rMin, rMax; bool player; };
    std::vector<Disk> disks;
    auto near = [&](Vec2 p) { for (auto& d : disks) if ((d.c - p).len() < 0.15) return true; return false; };
    for (size_t i = 0; i < fs.maxima.size() && disks.size() < 8; ++i) {
        const double rmax = std::min(1.2, fs.maxima[i].r - need - 0.01);
        if (rmax >= rLo && !near(fs.maxima[i].p)) disks.push_back({fs.maxima[i].p, rLo, rmax, false});
    }
    {   // the user's ring: Sonic around you
        const double r0 = room.playerRadius + need + 0.01;
        double r = r0, rOk = -1;
        while (r < r0 + 1.2 && ringClear(fs, room.player, r, need)) { rOk = r; r += 0.02; }
        if (rOk > 0) disks.push_back({room.player, r0, rOk, true});
    }
    {   // more lobe centres anywhere roomy enough (every 4th grid cell, a few picked at random)
        const Grid& F = fs.clearance;
        std::vector<Vec2> cand;
        for (int j = 0; j < F.ny; j += 4)
            for (int i = 0; i < F.nx; i += 4)
                if (F.at(i, j) >= need + rLo + 0.01) cand.push_back(F.pos(i, j));
        for (int t = 0; t < 12 && !cand.empty() && disks.size() < 14; ++t) {
            const Vec2 p = cand[rng.irange(0, (int)cand.size() - 1)];
            const double rmax = std::min(1.2, fs.clearance.sample(p) - need - 0.01);
            if (rmax >= rLo && !near(p)) disks.push_back({p, rLo, rmax, false});
        }
    }
    if (disks.size() < 2) return;
    const double G = (gp.layerGapU + gp.stackMarginU) * mpu;   // deck to deck where stacked
    const double lapMax = 26.0;
    const int flat = (int)std::ceil(44.0 / gp.stationU);
    int why[6] = {0, 0, 0, 0, 0, 0};   // rejected: no tangents, lap length, clearance, height budget, finalize, no profile
    std::vector<Skeleton> found;
    for (int trial = 0; trial < gp.coilTrials; ++trial) {
        const int k = disks.size() >= 3 && rng.chance(0.35) ? 3 : 2;
        std::vector<int> pick;
        while ((int)pick.size() < k) {
            const int d = rng.irange(0, (int)disks.size() - 1);
            if (std::find(pick.begin(), pick.end(), d) == pick.end()) pick.push_back(d);
        }
        std::vector<Vec2> c(k);
        std::vector<double> r(k), role(k);
        std::vector<int> s(k), w(k);
        for (int i = 0; i < k; ++i) {
            const Disk& d = disks[pick[i]];
            c[i] = d.c;
            r[i] = d.rMin + (d.rMax - d.rMin) * rng.range(0.45, 1.0);
            s[i] = rng.chance(0.5) ? 1 : -1;
        }
        // roles: +1 climbs, -1 descends, 0 level; a climb and a descent in every chain
        if (k == 2) { role[0] = 1; role[1] = -1; }
        else {
            static const double pats[4][3] = {{1, 1, -1}, {1, -1, -1}, {1, 0, -1}, {1, -1, 0}};
            const int p = rng.irange(0, 3);
            for (int i = 0; i < 3; ++i) role[i] = pats[p][i];
        }
        if (rng.chance(0.5)) std::reverse(role.begin(), role.end());
        // windings: a lobe winds over itself only if one revolution can climb (or fall) the stacking gap at its
        // walkable slope; two extra turns only if two gaps fit the height window
        const double window = gp.maxH - gp.minH - 0.1;
        for (int i = 0; i < k; ++i) {
            w[i] = 0;
            if (role[i] == 0) continue;
            const double ru = r[i] / mpu;
            const double perRev = (role[i] > 0 ? gp.walkRise : gp.walkFall) * walkBendFactor(gp, 1.0 / ru) * (role[i] > 0 ? gp.coilRiseSafety : gp.coilFallSafety) * 2 * kPi * r[i];
            if (perRev < G * 1.08) continue;
            const int wmax = 2 * G * 1.08 <= window ? 2 : 1;
            static const int winds[5] = {0, 1, 1, 1, 2};
            w[i] = std::min(wmax, winds[rng.irange(0, 4)]);
        }
        // Turning directions: neighbours turning opposite ways join by crossing lines (internal tangents), the same
        // way by outer ones. The lobes' circles are clear (their radii fit their disks), so only the lines need
        // checking: the first of the 2^k direction choices (from a random one on) whose lines are all clear.
        std::vector<Vec2> pin(k), pout(k);
        bool ok = false, tangents = false;
        const int combos = 1 << k, first = rng.irange(0, combos - 1);
        for (int t = 0; t < combos && !ok; ++t) {
            const int bits = (first + t) % combos;
            for (int i = 0; i < k; ++i) s[i] = (bits >> i) & 1 ? 1 : -1;
            bool all = true;
            for (int i = 0; i < k && all; ++i) {
                const int j = (i + 1) % k;
                all = tangentLine(c[i], s[i] * r[i], c[j], s[j] * r[j], pout[i], pin[j]);
            }
            if (!all) continue;
            tangents = true;
            ok = true;
            for (int i = 0; i < k && ok; ++i) {
                const Vec2 a = pout[i], b = pin[(i + 1) % k];
                const int n = std::max(2, (int)((b - a).len() / 0.02));
                for (int q = 0; q <= n && ok; ++q) ok = fs.clearance.sample(a + (b - a) * ((double)q / n)) >= need;
            }
        }
        if (!ok) { ++why[tangents ? 2 : 0]; continue; }
        // the path (dense), each piece's length and role for the guide heights
        std::vector<Vec2> path;
        std::vector<int> pieceOf;
        std::vector<double> pieceLen, pieceRole, pieceRate, pieceFactor;   // a wound lobe's rate: its revolution's rise or fall, per metre
        for (int i = 0; i < k; ++i) {
            const double a0 = std::atan2(pin[i].y - c[i].y, pin[i].x - c[i].x), a1 = std::atan2(pout[i].y - c[i].y, pout[i].x - c[i].x);
            double sweep = s[i] > 0 ? a1 - a0 : a0 - a1;
            while (sweep <= 1e-9) sweep += 2 * kPi;
            while (sweep > 2 * kPi) sweep -= 2 * kPi;
            sweep += 2 * kPi * w[i];
            const int na = std::max(4, (int)(sweep * r[i] / 0.01));
            for (int q = 0; q < na; ++q) {
                const double a = a0 + s[i] * sweep * q / na;
                path.push_back(c[i] + Vec2{std::cos(a), std::sin(a)} * r[i]);
                pieceOf.push_back((int)pieceLen.size());
            }
            pieceLen.push_back(sweep * r[i]);
            pieceRole.push_back(role[i]);
            pieceRate.push_back(w[i] > 0 ? G * 1.12 / (2 * kPi * r[i]) : 0.0);
            pieceFactor.push_back(walkBendFactor(gp, mpu / r[i]));
            const int j = (i + 1) % k;
            const double L = (pin[j] - pout[i]).len();
            const int nl = std::max(2, (int)(L / 0.01));
            for (int q = 0; q < nl; ++q) {
                path.push_back(pout[i] + (pin[j] - pout[i]) * ((double)q / nl));
                pieceOf.push_back((int)pieceLen.size());
            }
            // a line from a climbing lobe to a falling one tops out (level, or starts the fall), from falling to
            // climbing bottoms out (level, or starts the climb, often by a spring); otherwise it carries on
            const double ri = role[i], rj = role[j];
            double lineRole = ri > 0 && rj < 0 ? (rng.chance(0.5) ? 0.0 : -0.5) : ri < 0 && rj > 0 ? (rng.chance(0.4) ? 0.0 : 0.5)
                            : (ri + rj > 0 ? 0.7 : ri + rj < 0 ? -0.7 : 0.0);
            pieceLen.push_back(L);
            pieceRole.push_back(lineRole);
            pieceRate.push_back(0.0);
            pieceFactor.push_back(1.0);
        }
        double lap = 0;
        for (double L : pieceLen) lap += L;
        if (lap < 1.5 || lap > lapMax) { ++why[1]; continue; }
        bool clear = true;
        for (auto& p : path) if (fs.clearance.sample(p) < need) { clear = false; break; }
        if (!clear) { ++why[2]; continue; }
        // Guide heights: the lap climbs H and falls H. Wound lobes first get their revolution's gap (a little more);
        // the other climbing (falling) pieces share the rest of H by length and role; if there are none, the wound
        // lobes take it. The guide only orders the stacked pairs and steers the solver.
        const double lo = gp.minH + 0.03 + rng.range(0, 0.08), hi = gp.maxH - 0.03 - rng.range(0, 0.2);
        std::vector<double> rate(pieceLen.size(), 0.0);
        {
            // what the climbing (falling) pieces can do at their walkable slopes, less the start's level stretch
            double cap[2] = {-0.3 * gp.walkRise * gp.coilRiseSafety, -0.3 * gp.walkFall * gp.coilFallSafety};
            double needH[2] = {0, 0}, freeL[2] = {0, 0};   // [climb, fall]
            for (size_t q = 0; q < pieceLen.size(); ++q) {
                if (pieceRole[q] == 0) continue;
                const int sd = pieceRole[q] > 0 ? 0 : 1;
                needH[sd] += pieceRate[q] * pieceLen[q];
                cap[sd] += pieceLen[q] * (sd ? gp.walkFall * gp.coilFallSafety : gp.walkRise * gp.coilRiseSafety) * pieceFactor[q];
                if (pieceRate[q] == 0) freeL[sd] += std::fabs(pieceRole[q]) * pieceLen[q];
            }
            if (needH[0] + freeL[0] <= 0 || needH[1] + freeL[1] <= 0) { ++why[3]; continue; }
            const double H = std::min({hi - lo, cap[0], cap[1], std::max({needH[0], needH[1], (hi - lo) * rng.range(0.7, 1.0)})});
            if (H < 0.98 * std::max(needH[0], needH[1])) { ++why[3]; continue; }   // the wound lobes cannot make their gap

            for (size_t q = 0; q < pieceLen.size(); ++q) {
                if (pieceRole[q] == 0) continue;
                const int sd = pieceRole[q] > 0 ? 0 : 1;
                const double sign = sd ? -1.0 : 1.0;
                if (freeL[sd] > 0 && H >= needH[sd])
                    rate[q] = sign * (pieceRate[q] > 0 ? pieceRate[q] : std::fabs(pieceRole[q]) * (H - needH[sd]) / freeL[sd]);
                else   // only wound lobes this way, or H short of their need: theirs scaled to H
                    rate[q] = sign * pieceRate[q] * H / std::max(1e-9, needH[sd]);
            }
        }
        std::vector<double> guide(path.size());
        double cum = 0, cmin = 0, cmax = 0;
        for (size_t q = 0; q < path.size(); ++q) {
            guide[q] = cum;
            cmin = std::min(cmin, cum);
            cmax = std::max(cmax, cum);
            const Vec2 nx = path[(q + 1) % path.size()];
            cum += rate[pieceOf[q]] * (nx - path[q]).len();
        }
        {   // the lowest at lo, or the range centred on 0.85 m when it is small (a course you look at, not at your feet)
            const double base = std::max(lo, std::min(0.85 - 0.5 * (cmax - cmin), hi - (cmax - cmin)));
            for (auto& g : guide) g = base + (g - cmin);
        }
        Skeleton sk;
        sk.kind = "coil";
        sk.aroundPlayer = false;
        for (int i = 0; i < k; ++i) sk.aroundPlayer |= disks[pick[i]].player;
        sk.target = guide;
        if (!finalizeSkeleton(sk, path, fs, gp, nullptr, false)) { ++why[4]; continue; }
        if (sk.lengthM() > lapMax) continue;
        const std::vector<double> g2 = sk.target;
        if (!solveStackedProfile(sk, room, gp, g2, flat)) {
            if (gp.debug) {
                std::fprintf(stderr, "  coil trial %d: %d lobes, %.1f m: no stacked profile. lobes:", trial, k, sk.lengthM());
                for (int i = 0; i < k; ++i) std::fprintf(stderr, " [r %.2f w %d role %+.0f dir %+d%s]", r[i], w[i], role[i], s[i], disks[pick[i]].player ? " user" : "");
                std::fprintf(stderr, " lines:");
                for (size_t q = 1; q < pieceRole.size(); q += 2) std::fprintf(stderr, " %+.1f", pieceRole[q]);
                std::fprintf(stderr, "\n");
            }
            ++why[5];
            continue;
        }
        // how it uses the space: height range, stacked share, tiers
        const int N = sk.size();
        double zmin = 1e9, zmax = -1e9;
        int stacked = 0, tiers = 1;
        for (int j = 0; j < N; ++j) {
            zmin = std::min(zmin, sk.target[j]);
            zmax = std::max(zmax, sk.target[j]);
            if (sk.partners[j].empty()) continue;
            ++stacked;
            std::vector<double> h{sk.target[j]};
            for (int p : sk.partners[j]) h.push_back(sk.target[p]);
            std::sort(h.begin(), h.end());
            int t = 1;
            for (size_t q = 1; q < h.size(); ++q) t += h[q] - h[q - 1] > 0.5 * gp.layerGapU * mpu;
            tiers = std::max(tiers, t);
        }
        const double share = (double)stacked / N;
        if (stacked == 0 && zmax - zmin < 0.4) continue;   // neither stacked nor tall: the plain loops do that better
        sk.prelim = std::min(sk.lengthM(), 22.0) + 4 * (zmax - zmin) + 3 * (tiers - 1) + 3 * share + rng.range(0, 2);
        if (gp.debug)
            std::fprintf(stderr, "  coil trial %d: %d lobes (w %d %d %d), %.1f m, heights %.2f..%.2f, %d tiers, %.0f%% stacked, prelim %.1f\n", trial, k,
                         w[0], w[1], k > 2 ? w[2] : -1, sk.lengthM(), zmin, zmax, tiers, share * 100, sk.prelim);
        found.push_back(std::move(sk));
    }
    if (gp.debug)
        std::fprintf(stderr, "  coils: %zu of %d trials; rejected: tangents %d, lap length %d, clearance %d, height budget %d, resample %d, profile %d (%zu disks)\n",
                     found.size(), gp.coilTrials, why[0], why[1], why[2], why[3], why[4], why[5], disks.size());
    std::stable_sort(found.begin(), found.end(), [](const Skeleton& a, const Skeleton& b) { return a.prelim > b.prelim; });
    int kept = 0;
    std::vector<const Skeleton*> keep;
    for (auto& f : found) {
        if (kept >= gp.coilKeep) break;
        bool dup = false;
        for (auto* q : keep) dup |= std::fabs(q->lengthM() - f.lengthM()) < 0.3;
        if (dup) continue;
        keep.push_back(&f);
        ++kept;
    }
    for (auto* q : keep) out.push_back(*q);
}

}  // namespace rr
