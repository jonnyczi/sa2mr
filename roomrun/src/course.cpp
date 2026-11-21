#include "roomrun/course.hpp"

#include <algorithm>
#include <cmath>

namespace rr {

static double ease(double t) { return 0.5 - 0.5 * std::cos(kPi * std::clamp(t, 0.0, 1.0)); }

StationInfo stationInfo(const Placed& m, const ModuleDef& d, int j, const GenParams& gp) {
    StationInfo si;
    const int n = m.n(), k = j - m.s0;
    const double t = (double)k / n;
    const double mpu = gp.metersPerUnit();
    switch (m.kind) {
        case Kind::Start: case Kind::Track: case Kind::Dash: case Kind::Loop: case Kind::SkillRail:
            si.exists = true; si.walled = d.walls; si.z = m.h0; break;
        case Kind::Ramp:
            si.exists = true; si.walled = true; si.z = m.h0 + (m.h1 - m.h0) * ease(t); break;
        case Kind::Rail:
            si.exists = true; si.rail = true; si.walled = d.walls; si.z = m.h0 + (m.h1 - m.h0) * ease(t); break;
        case Kind::Drop:
            si.exists = true; si.walled = true; si.z = k < m.lipN ? m.h0 : m.h1; break;
        case Kind::Gap:
            if (k < m.lipN) { si.exists = true; si.walled = true; si.z = m.h0; }
            else if (k < m.lipN + m.holeN) { si.hole = true; si.z = m.h0 + (m.h1 - m.h0) * (double)(k - m.lipN) / std::max(1, m.holeN); }
            else { si.exists = true; si.walled = true; si.z = m.h1; }
            break;
        case Kind::Spring: {
            int landN = n - m.holeN - m.padN;
            if (k < m.padN) { si.exists = true; si.walled = true; si.z = m.h0; }
            else if (k >= n - landN) { si.exists = true; si.walled = true; si.z = m.h1; }
            else {
                // ballistic arc (time-parameterised, uniform horizontal speed)
                ScaledPhysics sp(gp.physics, gp.speedScale);
                double g = sp.p.gravity;
                double h0 = m.h0 / mpu, h1 = m.h1 / mpu, A = std::max(h0, h1) + d.param("apex", 16);
                double tu = std::sqrt(2 * (A - h0) / g), td = std::sqrt(2 * (A - h1) / g);
                double f = (double)(k - m.padN + 1) / (m.holeN + 1);
                double tt = f * (tu + td);
                double zz = h0 + g * tu * tt - 0.5 * g * tt * tt;
                si.z = zz * mpu;
            }
            break;
        }
        case Kind::Homing:
            if (k < m.lipN) { si.exists = true; si.walled = true; si.z = m.h0; }
            else if (k >= n - m.padN) { si.exists = true; si.walled = true; si.z = m.h1; }
            else {
                double f = (double)(k - m.lipN) / std::max(1, n - m.padN - m.lipN);
                si.hole = true;
                si.z = m.h0 + (m.h1 - m.h0) * f + d.param("lift", 12) * mpu;
            }
            break;
        default: break;
    }
    return si;
}

// Range of a run-off flight (no jump) from a lip with upward angle kick (deg) landing dh lower/higher.
static double runoffRange(double v, double kickDeg, double dhU, double g) {
    double a = kickDeg * kPi / 180.0;
    double vz = v * std::sin(a), vh = v * std::cos(a);
    double disc = vz * vz - 2 * g * dhU;
    if (disc < 0) return 0;
    return vh * (vz + std::sqrt(disc)) / g;
}

bool sizeGap(const ModuleDef& d, const Envelope& in, double dhU, const GenParams& gp, int& n, int& lipN, int& holeN) {
    ScaledPhysics sp(gp.physics, gp.speedScale);
    double g = sp.p.gravity, step = gp.stationU;
    double kick = d.param("kick", 12), ang = d.param("kickAngle", 6);
    double rLo = runoffRange(in.vMin, ang, dhU, g), rHi = runoffRange(in.vMax, ang, dhU, g);
    double G = std::min(d.param("maxGap", 90), d.param("rangeFactor", 0.6) * rLo);  // conservative: 60% of range
    if (G < d.param("minGap", 20)) return false;
    lipN = (int)std::ceil(kick / step);
    holeN = (int)std::floor(G / step);
    double land = std::max(24.0, rHi - holeN * step + d.param("margin", 20));
    n = lipN + holeN + (int)std::ceil(land / step);
    return n * step <= d.lenMax;
}

bool sizeDrop(const ModuleDef& d, const Envelope& in, double dhU, const GenParams& gp, int& n, int& lipN) {
    ScaledPhysics sp(gp.physics, gp.speedScale);
    double g = sp.p.gravity, step = gp.stationU;
    double range = in.vMax * std::sqrt(std::max(0.0, -2 * dhU / g));
    lipN = (int)std::ceil(d.param("lip", 8) / step);
    n = lipN + (int)std::ceil(std::max(24.0, range + d.param("margin", 20)) / step);
    return n * step <= d.lenMax && n * step >= d.lenMin;
}

Envelope transferEnvelope(const Placed& m, const ModuleDef& d, const Envelope& in, const Skeleton& sk,
                          const GenParams& gp) {
    if (d.fixedExit) return d.exit;
    ScaledPhysics sp(gp.physics, gp.speedScale);
    const Physics& p = sp.p;
    const double mpu = gp.metersPerUnit(), ds = gp.stationU;
    const double hwU = gp.trackWidthU * 0.5;
    Envelope out;
    double lo = in.vMin, hi = in.vMax;
    bool rail = m.kind == Kind::Rail;
    if (rail) lo = std::max(lo, d.param("minSpeed", 1.8));
    if (m.kind == Kind::Gap) lo *= std::cos(d.param("kickAngle", 6) * kPi / 180);
    StationInfo prev = stationInfo(m, d, m.s0, gp);
    for (int j = m.s0 + 1; j <= m.s1; ++j) {
        StationInfo cur = j < m.s1 ? stationInfo(m, d, j, gp) : prev;
        if (j == m.s1) cur.z = m.h1;
        double slope = (prev.exists && cur.exists) ? (cur.z - prev.z) / mpu / ds : 0.0;
        if (prev.exists && cur.exists) {
            double kap = std::fabs(sk.curv[(j - 1) % sk.size()]);
            if (rail) {
                double a = -p.gravity * p.slopeFactor * slope - d.param("friction", 0.003) * sp.k * sp.k;
                lo = std::sqrt(std::max(0.0, lo * lo + 2 * a * ds));
                lo = std::max(lo, d.param("minSpeed", 1.8));
                hi = std::sqrt(std::max(0.0, hi * hi + 2 * (-p.gravity * p.slopeFactor * slope) * ds));
            } else {
                // lower bound: stick released (coasting), wall scrape on tight curves
                double aLo = -p.coastDecel - p.gravity * p.slopeFactor * slope;
                lo = std::sqrt(std::max(0.0, lo * lo + 2 * aLo * ds));
                if (lo * lo * kap > p.grip + p.steerAccel && lo > 0.1) lo *= std::pow(p.wallScrape, ds / lo);
                // upper bound: full throttle
                double aHi = (hi < p.runMax ? p.runAccel : -p.dragAbove) - p.gravity * p.slopeFactor * slope;
                double nh = std::sqrt(std::max(0.0, hi * hi + 2 * aHi * ds));
                if (hi >= p.runMax && nh < p.runMax && slope <= 0) nh = p.runMax;
                hi = nh;
            }
        }
        hi = std::min(hi, p.topSpeed);
        lo = std::min(lo, hi);
        prev = cur;
    }
    if (d.param("speedCap", 0) > 0) { hi = std::min(hi, d.param("speedCap", 0)); lo = std::min(lo, hi); }
    out.vMin = lo;
    out.vMax = hi;
    out.lat = rail ? 1.0 : hwU - gp.physics.sonicRadius;
    out.head = rail ? 3.0 : 30.0;
    return out;
}

void bakeCourse(Course& c, const ModuleLibrary& lib, const GenParams& gp) {
    int N = c.sk.size();
    c.z.assign(N, 0); c.exists.assign(N, 0); c.walled.assign(N, 0); c.rail.assign(N, 0); c.hole.assign(N, 0);
    c.mod.assign(N, -1);
    for (size_t mi = 0; mi < c.mods.size(); ++mi) {
        const Placed& m = c.mods[mi];
        for (int j = m.s0; j < m.s1; ++j) {
            StationInfo si = stationInfo(m, lib.defs[m.def], j, gp);
            c.z[j] = si.z; c.exists[j] = si.exists; c.walled[j] = si.walled; c.rail[j] = si.rail;
            c.hole[j] = si.hole; c.mod[j] = (int)mi;
        }
    }
}

bool checkJoins(Course& c, const ModuleLibrary& lib, const GenParams& gp, int* bad) {
    ScaledPhysics sp(gp.physics, gp.speedScale);
    Envelope env{0, sp.p.topSpeed, 99, 180};  // start line: anything (standing start or flying lap)
    for (size_t i = 0; i < c.mods.size(); ++i) {
        Placed& m = c.mods[i];
        const ModuleDef& d = lib.defs[m.def];
        if (!env.subsetOf(d.entry)) { if (bad) *bad = (int)i; return false; }
        m.in = env;
        m.out = transferEnvelope(m, d, env, c.sk, gp);
        env = m.out;
    }
    return true;
}

}  // namespace rr
