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

    auto road = [&](int j) {
        const PlayKind k = pm.st[wrap(j, n)].kind;
        return k == PlayKind::Road || k == PlayKind::Start || k == PlayKind::Ramp || k == PlayKind::Loop || k == PlayKind::Bridge;   // a walled deck of road (loops painted; bridges: gaps, homing chains)
    };
    // A flight from a spring at station js to a landing at jl: its apex apexU above the higher end. Kept (and its
    // ring circle) when the game-physics check brings it down ahead within reach; false: left out (why noted).
    auto addSpring = [&](int js, int jl, double apexU, int reach, bool hop) {
        const Vec3 o = up(pm.st[js].p, op.springLift), t = pm.st[jl].p;
        const double h0 = o.y * u, h1 = t.y * u;
        const double vz = vzForApex(std::max(h0, h1) + apexU - h0, op);
        const double D = hyp(t.x - o.x, t.z - o.z) * u;
        if (D < 1e-6) return false;
        const double vh = vhForRange(D, vz, h1 - h0, op);
        const double dx = (t.x - o.x) * u / D, dz = (t.z - o.z) * u / D;
        CourseObject s;
        s.kind = ObjKind::Spring;
        s.p = pm.st[js].p;
        s.dir = {dx, 0, dz};
        s.station = js;
        s.launchU = {vh * dx, vz, vh * dz};
        const FlightCheck fc = checkFlight(pm, o, s.launchU, js, reach, op);
        const int ahead = fc.landStation < 0 ? -1 : wrap(fc.landStation - js, n);
        if (fc.landStation < 0 || ahead < 2) {
            if (!hop) note("spring at %d: %s", js, fc.landStation < 0 ? fc.why.c_str() : "lands behind its take-off");
            return false;
        }
        s.landStation = fc.landStation;
        s.apexU = fc.apexU;
        s.flightFrames = fc.frames;
        out.objs.push_back(s);
        take(js - 6, js + ahead + 2);
        if (op.rings) {   // a ring circle at the flight's top
            const FlightCheck path = checkFlight(pm, o, s.launchU, js, reach, op, true);
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
        return true;
    };
    // springs: on the take-off pad's last station, launched to the landing pad's first third
    int springs = 0;
    for (size_t mi = 0; mi < c.mods.size() && op.springs; ++mi) {
        const Placed& m = c.mods[mi];
        if (m.kind != Kind::Spring) continue;
        const ModuleDef& d = lib.defs[m.def];
        const int landN = m.n() - m.holeN - m.padN;
        const int js = wrap(m.s0 + std::max(0, m.padN - 1), n), jl = wrap(m.s1 - landN + landN / 3, n);
        if (addSpring(js, jl, d.param("apex", op.springApexU), m.n() + 8, false)) springs++;
        else out.stats.springsDropped++;
    }
    // dash panels: a boost module's second station, along the course
    int dashes = 0;
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
        dashes++;
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
    // enemies: GUN Beetles (op.hunterShare Hunters) on flat road, never on a landing, a rail or right after a
    // spring (the objects' ranges), hovering op.beetleHeightU up so he runs under them and jumps to attack
    if (op.enemiesPerM > 0) {
        const int want = std::min(op.enemyMax, std::max(c.sk.lengthM() >= 2.5 ? op.enemyMin : 0,
                                                        (int)std::floor(c.sk.lengthM() * op.enemiesPerM)));
        const int every = want > 0 ? std::max(12, n / want) : n;
        int placed = 0;
        for (int pass = 0; pass < 2 && placed < want; ++pass)   // a narrower flat stretch the second time
        for (int k = 0; k < n && placed < want; ++k) {
            const int j = wrap(every / 2 + k, n);
            const PlayStation& s = pm.st[j];
            if (taken[j] || pm.st[j].kind == PlayKind::Rail || !road(j) || (s.kind == PlayKind::Start && !pass)) continue;   // the start straight only the second time
            bool flat = true;
            for (int q = -(pass ? 2 : 4); q <= (pass ? 2 : 4); ++q) {
                const PlayStation& a = pm.st[wrap(j + q, n)];
                flat &= !taken[wrap(j + q, n)] && pm.st[wrap(j + q, n)].kind != PlayKind::Rail && std::fabs(a.p.y - s.p.y) * u < 3.0 && road(j + q);
            }
            if (!flat) continue;
            CourseObject e;
            const int hunters = (int)std::floor((placed + 1) * op.hunterShare + 1e-9) - (int)std::floor(placed * op.hunterShare + 1e-9);
            e.kind = hunters ? ObjKind::Hunter : ObjKind::Beetle;
            e.p = e.kind == ObjKind::Beetle ? up(s.p, op.beetleHeightU) : s.p;
            e.dir = s.fwd;
            e.station = j;
            out.objs.push_back(e);
            take(j - 6, j + 6);
            placed++;
            k += every - 1;
        }
    }
    // Variety (the Frame, 2026-11-21: a 2 x 2 m course came out rails and rings only). A course without a spring
    // module gets a hop spring: a short flight from its road onto the road ahead, checked as the others; without
    // a dash module, a dash panel on a straight stretch. Searched from a third of the way round (away from the
    // start line), road first, then the rail strips (a walled deck as well).
    auto free = [&](int a, int b, bool roadOnly) {
        for (int q = a; q <= b; ++q)
            if (taken[wrap(q, n)] || (roadOnly && !road(q))) return false;
        return true;
    };
    if (op.ensureVariety && op.springs && springs == 0)
        for (int pass = 0; pass < 2 && !springs; ++pass)
            for (int k = 0; k < n && !springs; ++k) {
                const int js = wrap(n / 3 + k, n);
                for (int a = op.hopMaxStations; a >= op.hopMinStations && !springs; --a)
                    if (free(js - 2, js + a + 2, pass == 0))
                        if (addSpring(js, wrap(js + a, n), op.hopApexU, a + 6, true)) { springs++; out.stats.hopSprings++; }
            }
    if (op.ensureVariety && op.dashPanels && dashes == 0)
        for (int pass = 0; pass < 3 && !dashes; ++pass)
            for (int k = 0; k < n && !dashes; ++k) {
                const int j = wrap(n / 3 + k, n);
                const int run = pass < 2 ? 10 : 6;   // the third time a shorter, bendier stretch (within 45 degrees)
                if (!free(j - 2, j + run, pass == 0)) continue;
                bool straight = true;   // the boost runs into no tight bend: the way within 30 degrees for 10 stations
                for (int q = 1; q <= run && straight; ++q) {
                    const Vec3& a = pm.st[j].fwd;
                    const Vec3& b = pm.st[wrap(j + q, n)].fwd;
                    straight = a.x * b.x + a.z * b.z > (pass < 2 ? 0.866 : 0.707);
                }
                if (!straight) continue;
                CourseObject o;
                o.kind = ObjKind::DashPanel;
                o.p = pm.st[j].p;
                o.dir = pm.st[j].fwd;
                o.station = j;
                o.speedU = op.smallDashU;
                o.lockFrames = op.smallDashLock;
                out.objs.push_back(o);
                take(j - 6, j + 10);
                dashes++;
                out.stats.addedDash++;
            }
    // grind rails: each run of rail stations (runs op.railJoinStations apart joined over the road between), then
    // paths of their own over long road stretches until op.railTargetShare of the course is under one. Out of
    // reach running (op.railHeightU up): a jump gets him on, and off it the strip or road below always runs.
    if (op.rails) {
        std::vector<uint8_t> want(n, 0);
        for (int j = 0; j < n; ++j) want[j] = pm.st[j].kind == PlayKind::Rail;
        out.stats.railStations = 0;
        for (int j = 0; j < n; ++j) out.stats.railStations += want[j];
        for (int j = 0; j < n; ++j)   // join: a short break between rail stations
            if (want[j] && !want[wrap(j + 1, n)])
                for (int g = 2; g <= op.railJoinStations + 1; ++g)
                    if (want[wrap(j + g, n)]) {
                        bool ok = true;
                        for (int q = 1; q < g; ++q) ok &= !taken[wrap(j + q, n)];
                        if (ok) for (int q = 1; q < g; ++q) want[wrap(j + q, n)] = 1;
                        break;
                    }
        for (int j = 0; j < n; ++j) if (taken[j]) want[j] = 0;   // springs, panels, ramps: never under a path
        int under = 0;
        for (int j = 0; j < n; ++j) under += want[j];
        std::vector<uint8_t> roadRail(n, 0);
        if (under < op.railTargetShare * n) {   // road stretches of their own, the longest first
            struct Run { int a, len; };
            std::vector<Run> runs;
            int s0 = 0;
            while (s0 < n && (road(s0) && !taken[s0] && !want[s0])) ++s0;
            for (int k = 0; k < n;) {
                const int j = wrap(s0 + k, n);
                if (!(road(j) && !taken[j] && !want[j])) { ++k; continue; }
                int len = 0;
                while (len < n && road(j + len) && !taken[wrap(j + len, n)] && !want[wrap(j + len, n)]) ++len;
                if (len >= op.roadRailMinStations + 6) runs.push_back({j + 3, len - 6});
                k += len;
            }
            std::stable_sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) { return a.len > b.len; });
            for (const Run& r : runs) {
                if (under >= op.railTargetShare * n) break;
                const int len = std::min(r.len, (int)std::ceil(op.railTargetShare * n) - under + 4);
                if (len < op.roadRailMinStations) continue;
                for (int q = 0; q < len; ++q) { want[wrap(r.a + q, n)] = 1; roadRail[wrap(r.a + q, n)] = 1; }
                under += len;
            }
        }
        int start = 0;
        while (start < n && want[start]) ++start;
        if (start < n)
            for (int k = 0; k < n;) {
                const int j = wrap(start + k, n);
                if (!want[j]) { ++k; continue; }
                int len = 0;
                while (len < n && want[wrap(j + len, n)]) ++len;
                CourseRail rl;
                rl.s0 = j;
                rl.s1 = j + len;
                rl.overRoad = roadRail[j] != 0;
                double rise = 0;
                for (int i = 0; i <= len && len >= 4; ++i) {   // to the first station after the run: the path ends there
                    const PlayStation& s = pm.st[wrap(j + i, n)];
                    rl.pts.push_back(up(s.p, op.railHeightU));
                    rise = std::max(rise, (s.p.y - pm.st[j].p.y) * u);
                }
                rl.riseU = rise;
                if (len < 4) note("rail at %d: %d stations, too short for a grind path", j, len);
                else if (rise > op.railMaxRiseU) {
                    out.stats.railsDropped++;
                    note("rail at %d: climbs %.0f units (over %.0f): the strip only", j, rise, op.railMaxRiseU);
                } else {
                    out.rails.push_back(rl);
                    out.stats.roadRails += rl.overRoad;
                    out.stats.grindStations += len;
                }
                k += len;
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
    char b[448];
    std::snprintf(b, sizeof b,
                  "%d rings (%d lines, %d circles), %d springs (%d left out), %d dash panels, %d dash ramps (%d left out), "
                  "%d grind rails (%d over road) under %d of the stations (%d rail-strip stations), %d enemies; added for variety: %d hop "
                  "springs, %d dash panels",
                  s.rings, s.ringLines, s.ringCircles, s.springs, s.springsDropped, s.dashPanels, s.dashRamps, s.rampsDropped,
                  s.rails, s.roadRails, s.grindStations, s.railStations, s.enemies, s.hopSprings, s.addedDash);
    return b;
}

}  // namespace rr

namespace rr {

// The grind paths as something to see: a bar along each (railBarU thick) and a post down to the deck every
// few points, in the play mesh's frame. The game's rails are landtable art the course does not have.
PlayGroup grindRailGroup(const CourseObjects& co, double mpu, const ObjectParams& op) {
    PlayGroup g;
    g.name = "grind_rail";
    g.rgb[0] = 0.85f; g.rgb[1] = 0.88f; g.rgb[2] = 0.95f;
    auto tri = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        const Vec3 e1 = b - a, e2 = c - a;
        Vec3 nn{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        const double l = nn.len();
        if (l < 1e-14) return;
        nn = nn * (1.0 / l);
        for (const Vec3* v : {&a, &b, &c}) { g.pos.push_back((float)v->x); g.pos.push_back((float)v->y); g.pos.push_back((float)v->z); }
        g.nrm.push_back((float)nn.x); g.nrm.push_back((float)nn.y); g.nrm.push_back((float)nn.z);
    };
    auto quad = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) { tri(a, b, c); tri(a, c, d); };
    const double h = 0.5 * op.railBarU * mpu;
    for (const auto& rl : co.rails) {
        for (size_t i = 0; i + 1 < rl.pts.size(); ++i) {
            const Vec3 a = rl.pts[i], b = rl.pts[i + 1];
            double sx = -(b.z - a.z), sz = b.x - a.x;
            const double sl = std::sqrt(sx * sx + sz * sz);
            if (sl < 1e-9) continue;
            sx *= h / sl; sz *= h / sl;
            // four sides of a square bar (the side vector s, up h), counter-clockwise from outside
            const Vec3 a0{a.x - sx, a.y - h, a.z - sz}, a1{a.x + sx, a.y - h, a.z + sz}, a2{a.x + sx, a.y + h, a.z + sz}, a3{a.x - sx, a.y + h, a.z - sz};
            const Vec3 b0{b.x - sx, b.y - h, b.z - sz}, b1{b.x + sx, b.y - h, b.z + sz}, b2{b.x + sx, b.y + h, b.z + sz}, b3{b.x - sx, b.y + h, b.z - sz};
            quad(a3, a2, b2, b3);   // top
            quad(a0, b0, b1, a1);   // bottom
            quad(a1, b1, b2, a2);   // +s
            quad(a0, a3, b3, b0);   // -s
            if (i % 4 == 0) {       // a post down to the deck
                const double ph = 0.35 * op.railBarU * mpu, top = a.y - h, bot = a.y - op.railHeightU * mpu;
                const Vec3 p0{a.x - ph, bot, a.z - ph}, p1{a.x + ph, bot, a.z - ph}, p2{a.x + ph, bot, a.z + ph}, p3{a.x - ph, bot, a.z + ph};
                const Vec3 q0{a.x - ph, top, a.z - ph}, q1{a.x + ph, top, a.z - ph}, q2{a.x + ph, top, a.z + ph}, q3{a.x - ph, top, a.z + ph};
                quad(p0, p1, q1, q0); quad(p1, p2, q2, q1); quad(p2, p3, q3, q2); quad(p3, p0, q0, q3);
            }
        }
    }
    return g;
}

}  // namespace rr
