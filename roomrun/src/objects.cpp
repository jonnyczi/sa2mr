#include "roomrun/objects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rr {

const char* objKindName(ObjKind k) {
    switch (k) {
        case ObjKind::Ring: return "ring";
        case ObjKind::RingLine: return "ring_line";
        case ObjKind::RingCircle: return "ring_circle";
        case ObjKind::Spring: return "spring";
        case ObjKind::DashPanel: return "dash_panel";
        case ObjKind::DashRamp: return "dash_ramp";
        case ObjKind::Beetle: return "beetle";
        case ObjKind::Hunter: return "hunter";
    }
    return "?";
}

namespace {

int wrap(int j, int n) { return ((j % n) + n) % n; }
double hyp(double x, double z) { return std::sqrt(x * x + z * z); }

// The centreline segment j..j+1 nearest a plan point: its station, the fraction along it, the lateral
// distance, and the deck's height there (metres).
struct Near { int j = -1; double t = 0, lat = 1e9, deck = 0; };
Near nearSeg(const PlayMesh& pm, int j, double x, double z) {
    const int n = (int)pm.st.size();
    const Vec3& a = pm.st[wrap(j, n)].p;
    const Vec3& b = pm.st[wrap(j + 1, n)].p;
    const double dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
    double t = l2 > 1e-12 ? ((x - a.x) * dx + (z - a.z) * dz) / l2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    Near r;
    r.j = wrap(j, n);
    r.t = t;
    r.lat = hyp(x - (a.x + t * dx), z - (a.z + t * dz));
    r.deck = a.y + t * (b.y - a.y);
    return r;
}

// The vertical launch speed for an apex `riseU` above the take-off under gravity and drag (bisection).
double vzForApex(double riseU, const ObjectParams& op) {
    auto apex = [&](double vz) {
        double y = 0, v = vz, top = 0;
        for (int f = 0; f < 2000 && v > 0; ++f) { v -= op.gravityU; v *= 1.0 - op.airDrag; y += v; top = std::max(top, y); }
        return top;
    };
    double lo = 0.05, hi = 12.0;
    for (int it = 0; it < 60; ++it) {
        const double m = 0.5 * (lo + hi);
        (apex(m) < riseU ? lo : hi) = m;
    }
    return 0.5 * (lo + hi);
}
// The horizontal distance a flight (vh, vz) covers before it comes down to dyU (below the take-off: < 0).
double rangeOf(double vh, double vz, double dyU, const ObjectParams& op, int hold = 0, double drag = -1) {
    if (drag < 0) drag = op.airDrag;
    double x = 0, y = 0, h = vh, v = vz;
    for (int f = 0; f < 4000; ++f) {
        if (f >= hold) v -= op.gravityU;
        v *= 1.0 - drag;
        h *= 1.0 - drag;
        x += h;
        y += v;
        if (v < 0 && y <= dyU) return x;
    }
    return x;
}
double vhForRange(double D, double vz, double dyU, const ObjectParams& op) {
    double lo = 0.0, hi = 20.0;
    for (int it = 0; it < 60; ++it) {
        const double m = 0.5 * (lo + hi);
        (rangeOf(m, vz, dyU, op) < D ? lo : hi) = m;
    }
    return 0.5 * (lo + hi);
}

}  // namespace

FlightCheck checkFlight(const PlayMesh& pm, const Vec3& from, const Vec3& vU, int fromStation, int maxAhead,
                        const ObjectParams& op, bool sample, int hold, double drag) {
    if (drag < 0) drag = op.airDrag;
    FlightCheck fc;
    const int n = (int)pm.st.size();
    const double u = 1.0 / pm.mpu;
    Vec3 p{from.x * u, from.y * u, from.z * u}, v = vU;
    const double y0 = p.y;
    int hint = fromStation;
    for (int f = 1; f <= 600; ++f) {
        if (f > hold) v.y -= op.gravityU;
        v = v * (1.0 - drag);
        p = p + v;
        fc.apexU = std::max(fc.apexU, p.y - y0);
        if (sample) fc.path.push_back(p * pm.mpu);
        // the course under him: the nearest segment from the last one on, within the module's reach
        const double x = p.x * pm.mpu, z = p.z * pm.mpu, y = p.y * pm.mpu;
        Near best;
        int bestK = -1;
        for (int k = 0; k <= maxAhead; ++k) {
            const int j = wrap(hint + k, n);
            if (wrap(j - fromStation, n) > maxAhead) break;
            Near q = nearSeg(pm, j, x, z);
            if (q.lat < best.lat - 1e-9) { best = q; bestK = k; }
        }
        if (bestK < 0) { fc.why = "no station ahead"; return fc; }
        hint = best.j;
        const double halfW = pm.st[best.j].halfW;
        if (best.lat > halfW - op.sonicRadiusU * pm.mpu) {
            char b[96];
            std::snprintf(b, sizeof b, "left the walkway at frame %d (%.1f units off the centreline)", f, best.lat * u);
            fc.why = b;
            return fc;
        }
        // a deck above him (a crossing): his head under its underside
        for (int q = 0; q < n; ++q) {
            const PlayStation& s = pm.st[q];
            if (s.p.y <= y + 1e-6 || hyp(s.p.x - x, s.p.z - z) > s.halfW + op.sonicRadiusU * pm.mpu) continue;
            if (wrap(q - best.j, n) <= 12 || wrap(best.j - q, n) <= 12) continue;   // the course itself, here
            if (y + (op.sonicHeightU + 2.0) * pm.mpu > s.p.y) {
                char b[96];
                std::snprintf(b, sizeof b, "met the deck of station %d above at frame %d", q, f);
                fc.why = b;
                return fc;
            }
        }
        if (v.y < 0 && y <= best.deck + 1e-9) {
            fc.landStation = best.j;
            fc.frames = f;
            return fc;
        }
    }
    fc.why = "never came down";
    return fc;
}

CourseObjects placeObjects(const GenResult& r, const PlayMesh& pm, const ModuleLibrary& lib, const GenParams& gp,
                           const ObjectParams& op) {
    CourseObjects out;
    const Course& c = r.course;
    const int n = (int)pm.st.size();
    if (n < 8 || (int)c.mod.size() != n) return out;
    const double mpu = pm.mpu, u = 1.0 / mpu;
    const double stepU = gp.stationU;
    std::vector<uint8_t> taken(n, 0);   // stations an object (or its approach) occupies: no rings there
    auto take = [&](int a, int b) { for (int j = a; j <= b; ++j) taken[wrap(j, n)] = 1; };
    auto up = [&](const Vec3& p, double du) { return Vec3{p.x, p.y + du * mpu, p.z}; };
    auto note = [&](const char* fmt, auto... a) {
        char b[192];
        std::snprintf(b, sizeof b, fmt, a...);
        if (out.stats.notes.size() < 24) out.stats.notes.push_back(b);
    };

    // grind rails: each run of rail stations, its centreline just above the strip
    if (op.rails) {
        int start = 0;
        while (start < n && pm.st[start].kind == PlayKind::Rail) ++start;
        if (start < n)
            for (int k = 0; k < n;) {
                const int j = wrap(start + k, n);
                if (pm.st[j].kind != PlayKind::Rail) { ++k; continue; }
                int len = 0;
                while (len < n && pm.st[wrap(j + len, n)].kind == PlayKind::Rail) ++len;
                CourseRail rl;
                rl.s0 = j;
                rl.s1 = j + len;
                double rise = 0;
                for (int i = 0; i <= len && len >= 4; ++i) {   // to the first station after the run: the path ends on the road
                    const PlayStation& s = pm.st[wrap(j + i, n)];
                    rl.pts.push_back(up(s.p, op.railHeightU));
                    rise = std::max(rise, (s.p.y - pm.st[j].p.y) * u);
                }
                rl.riseU = rise;
                out.stats.railStations += len;
                if (len < 4) note("rail at %d: %d stations, too short for a grind path", j, len);
                else if (rise > op.railMaxRiseU) {
                    out.stats.railsDropped++;
                    note("rail at %d: climbs %.0f units (over %.0f): the strip only", j, rise, op.railMaxRiseU);
                } else {
                    out.rails.push_back(rl);
                }
                k += len;
            }
    }
    // springs: on the take-off pad's last station, launched to the landing pad's first third
    for (size_t mi = 0; mi < c.mods.size(); ++mi) {
        const Placed& m = c.mods[mi];
        if (m.kind != Kind::Spring || !op.springs) continue;
        const ModuleDef& d = lib.defs[m.def];
        const int landN = m.n() - m.holeN - m.padN;
        const int js = wrap(m.s0 + std::max(0, m.padN - 1), n), jl = wrap(m.s1 - landN + landN / 3, n);
        const Vec3 o = up(pm.st[js].p, op.springLift), t = pm.st[jl].p;
        const double h0 = o.y * u, h1 = t.y * u;
        const double rise = std::max(h0, h1) + d.param("apex", op.springApexU) - h0;
        const double vz = vzForApex(rise, op);
        const double D = hyp(t.x - o.x, t.z - o.z) * u;
        const double vh = vhForRange(D, vz, h1 - h0, op);
        const double dx = (t.x - o.x) * u / std::max(1e-9, D), dz = (t.z - o.z) * u / std::max(1e-9, D);
        CourseObject s;
        s.kind = ObjKind::Spring;
        s.p = pm.st[js].p;
        s.dir = {dx, 0, dz};
        s.station = js;
        s.launchU = {vh * dx, vz, vh * dz};
        const FlightCheck fc = checkFlight(pm, o, s.launchU, js, m.n() + 8, op);
        const int ahead = fc.landStation < 0 ? -1 : wrap(fc.landStation - js, n);
        if (fc.landStation < 0 || ahead < m.padN / 2 + 1) {
            out.stats.springsDropped++;
            note("spring at %d: %s", js, fc.landStation < 0 ? fc.why.c_str() : "lands behind its take-off");
            continue;
        }
        s.landStation = fc.landStation;
        s.apexU = fc.apexU;
        s.flightFrames = fc.frames;
        out.objs.push_back(s);
        take(js - 6, m.s1);
        // a ring circle at the flight's top
        if (op.rings) {
            const FlightCheck path = checkFlight(pm, o, s.launchU, js, m.n() + 8, op, true);
            size_t top = 0;
            for (size_t i = 0; i < path.path.size(); ++i) if (path.path[i].y > path.path[top].y) top = i;
            if (!path.path.empty()) {
                CourseObject rc;
                rc.kind = ObjKind::RingCircle;
                rc.p = up(path.path[top], 5.0);   // his middle
                rc.dir = s.dir;
                rc.station = js;
                rc.count = op.ringCircleCount;
                rc.radiusU = op.ringCircleRadiusU;
                out.objs.push_back(rc);
            }
        }
    }
    // dash panels: a boost module's second station, along the course
    for (size_t mi = 0; mi < c.mods.size() && op.dashPanels; ++mi) {
        const Placed& m = c.mods[mi];
        if (m.kind != Kind::Dash) continue;
        const ModuleDef& d = lib.defs[m.def];
        const int j = wrap(m.s0 + 1, n);
        CourseObject o;
        o.kind = ObjKind::DashPanel;
        o.p = pm.st[j].p;
        o.dir = pm.st[j].fwd;
        o.station = j;
        o.speedU = d.param("speed", 5.4) / gp.speedScale;   // the library's speeds are scaled
        o.lockFrames = (int)std::lround(d.param("lock", 30));
        out.objs.push_back(o);
        take(j - 6, m.s1 + 2);
    }
    // dash ramps: at a gap's lip, launched over it onto the landing's first third
    for (size_t mi = 0; mi < c.mods.size() && op.dashRamps; ++mi) {
        const Placed& m = c.mods[mi];
        if (m.kind != Kind::Gap) continue;
        const int landN = m.n() - m.lipN - m.holeN;
        const int jr = wrap(m.s0 + std::max(0, m.lipN - 1), n), jl = wrap(m.s1 - landN + landN / 3, n);
        const Vec3 o = pm.st[jr].p, t = pm.st[jl].p;
        const double D = hyp(t.x - o.x, t.z - o.z) * u, dy = (t.y - o.y) * u;
        // launched at op.rampKickDeg: the forward speed that lands at D (bisection; vz = vh tan(kick))
        const double k = std::tan(op.rampKickDeg * kPi / 180.0);
        double lo = 0.2, hi = 20.0;
        for (int it = 0; it < 60; ++it) {
            const double mm = 0.5 * (lo + hi);
            (rangeOf(mm, mm * k, dy, op, op.rampHoldFrames, op.rampDrag) < D ? lo : hi) = mm;
        }
        const double vh = 0.5 * (lo + hi), vz = vh * k;
        const double dx = (t.x - o.x) * u / std::max(1e-9, D), dz = (t.z - o.z) * u / std::max(1e-9, D);
        CourseObject rp;
        rp.kind = ObjKind::DashRamp;
        rp.p = o;
        rp.dir = {dx, 0, dz};
        rp.station = jr;
        rp.launchU = {vh * dx, vz, vh * dz};
        const FlightCheck fc = checkFlight(pm, o, rp.launchU, jr, m.n() + 8, op, false, op.rampHoldFrames, op.rampDrag);
        if (fc.landStation < 0 || wrap(fc.landStation - jr, n) < 2) {
            out.stats.rampsDropped++;
            note("dash ramp at %d: %s", jr, fc.landStation < 0 ? fc.why.c_str() : "lands at its lip");
            continue;
        }
        rp.landStation = fc.landStation;
        rp.apexU = fc.apexU;
        rp.flightFrames = fc.frames;
        out.objs.push_back(rp);
        take(jr - 8, m.s1);
    }
    // enemies (optional): on flat road away from everything else
    if (op.enemiesPerM > 0) {
        const int want = std::min(op.enemyMax, (int)std::floor(c.sk.lengthM() * op.enemiesPerM));
        const int every = want > 0 ? std::max(12, n / want) : n;
        int placed = 0;
        for (int j = every / 2; j < n && placed < want; ++j) {
            const PlayStation& s = pm.st[j];
            if (taken[j] || (s.kind != PlayKind::Road && s.kind != PlayKind::Ramp)) continue;
            bool flat = true;
            for (int q = -4; q <= 4; ++q) {
                const PlayStation& a = pm.st[wrap(j + q, n)];
                flat &= !taken[wrap(j + q, n)] && std::fabs(a.p.y - s.p.y) * u < 2.0 && a.kind != PlayKind::Rail;
            }
            if (!flat) continue;
            CourseObject e;
            e.kind = placed % 2 ? ObjKind::Hunter : ObjKind::Beetle;
            e.p = e.kind == ObjKind::Beetle ? up(s.p, 12.0) : s.p;
            e.dir = s.fwd;
            e.station = j;
            out.objs.push_back(e);
            take(j - 6, j + 6);
            placed++;
            j += every - 1;
        }
    }
    // rings: straight flat stretches of road get a line; elsewhere three single rings following the bend
    if (op.rings) {
        const int ss = std::max(1, (int)std::lround(op.ringSpacingU / stepU));
        auto eligible = [&](int j) {
            const PlayStation& s = pm.st[wrap(j, n)];
            return !taken[wrap(j, n)] && (s.kind == PlayKind::Road || s.kind == PlayKind::Start || s.kind == PlayKind::Ramp);
        };
        auto single = [&](int j) {     // single rings following a bend: road, or a rail (over its grind path)
            return eligible(j) || (!taken[wrap(j, n)] && pm.st[wrap(j, n)].kind == PlayKind::Rail);
        };
        auto lift = [&](int j) { return op.ringHeightU + (pm.st[wrap(j, n)].kind == PlayKind::Rail ? op.railHeightU : 0.0); };
        int start = 0;
        while (start < n && eligible(start)) ++start;   // begin where a run begins
        if (start >= n) start = 0;
        int rings = 0;
        for (int k = 0; k < n && rings < 150;) {
            const int j = wrap(start + k, n);
            if (!single(j)) { ++k; continue; }
            int best = 0;
            for (int cnt = std::min(op.ringLineMax, 8); cnt >= 3 && !best; --cnt) {
                const int last = j + (cnt - 1) * ss;
                if (k + (cnt - 1) * ss >= n) continue;
                bool ok = true;
                for (int q = j; q <= last && ok; ++q) ok = eligible(q);
                if (!ok) continue;
                const Vec3 a = pm.st[j].p, b = pm.st[wrap(last, n)].p;
                const double L = hyp(b.x - a.x, b.z - a.z);
                if (L < 1e-6 || std::fabs(b.y - a.y) > 0.45 * L) continue;   // a line may climb (its pitch is the game's rotation)
                for (int q = j; q <= last && ok; ++q) {
                    const Vec3 p = pm.st[wrap(q, n)].p;
                    const double t = ((p.x - a.x) * (b.x - a.x) + (p.z - a.z) * (b.z - a.z)) / (L * L);
                    const double lat = hyp(p.x - (a.x + t * (b.x - a.x)), p.z - (a.z + t * (b.z - a.z)));
                    ok = lat * u <= op.straightTolU && std::fabs(p.y - (a.y + t * (b.y - a.y))) * u <= 1.5;
                }
                if (ok) best = cnt;
            }
            if (best) {
                const int last = j + (best - 1) * ss;
                const Vec3 a = pm.st[j].p, b = pm.st[wrap(last, n)].p;
                const double L = (b - a).len();
                CourseObject o;
                o.kind = ObjKind::RingLine;
                o.p = up(a, op.ringHeightU);
                o.dir = (b - a) * (1.0 / L);   // along the chord, its climb included
                o.station = j;
                o.count = best;
                o.spacingU = L * u / (best - 1);
                out.objs.push_back(o);
                rings += best;
                k += (best - 1) * ss + op.ringGapStations;
                continue;
            }
            bool three = k + 2 * ss < n;
            for (int q = j; q <= j + 2 * ss && three; ++q) three = single(q);
            if (three) {
                for (int i = 0; i < 3; ++i) {
                    const int q = wrap(j + i * ss, n);
                    CourseObject o;
                    o.kind = ObjKind::Ring;
                    o.p = up(pm.st[q].p, lift(q));
                    o.dir = pm.st[q].fwd;
                    o.station = q;
                    o.count = 1;
                    out.objs.push_back(o);
                }
                rings += 3;
                k += 2 * ss + op.ringGapStations;
                continue;
            }
            ++k;
        }
    }
    for (const auto& o : out.objs) {
        switch (o.kind) {
            case ObjKind::Ring: out.stats.rings++; break;
            case ObjKind::RingLine: out.stats.rings += o.count; out.stats.ringLines++; break;
            case ObjKind::RingCircle: out.stats.rings += o.count; out.stats.ringCircles++; break;
            case ObjKind::Spring: out.stats.springs++; break;
            case ObjKind::DashPanel: out.stats.dashPanels++; break;
            case ObjKind::DashRamp: out.stats.dashRamps++; break;
            case ObjKind::Beetle: case ObjKind::Hunter: out.stats.enemies++; break;
        }
    }
    out.stats.rails = (int)out.rails.size();
    return out;
}

std::string objectSummary(const CourseObjects& o) {
    const ObjectStats& s = o.stats;
    char b[320];
    std::snprintf(b, sizeof b,
                  "%d rings (%d lines, %d circles), %d springs (%d left out), %d dash panels, %d dash ramps (%d left out), "
                  "%d grind rails over %d rail stations (%d left out), %d enemies",
                  s.rings, s.ringLines, s.ringCircles, s.springs, s.springsDropped, s.dashPanels, s.dashRamps, s.rampsDropped,
                  s.rails, s.railStations, s.railsDropped, s.enemies);
    return b;
}

}  // namespace rr
